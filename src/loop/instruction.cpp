#include "phoenix/loop/instruction.hpp"

#include "phoenix/scripting/quickjs_engine.hpp"

#include <sstream>
#include <utility>

namespace phoenix::loop {
namespace {

const RuntimeValue* find_input(const InstructionExecutionFrame& frame, const PortId& port)
{
    for (const auto& input : frame.inputs.promised_inputs)
        if (input.port == port) return &input.value;
    return nullptr;
}

ActorId owner(const InstructionExecutionFrame& frame, const GeometryValue& geometry)
{
    if (geometry.accumulation_actor_id) return *geometry.accumulation_actor_id;
    const auto* call = frame.call_stack.current();
    return call && call->actor_id ? *call->actor_id : ActorId{"root"};
}

} // namespace

InstructionHandler make_instruction_handler(InstructionConfig config)
{
    return [config = std::move(config)](const InstructionExecutionFrame& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        const auto* value = find_input(frame, config.geometry_input_port);
        const auto* geometry = value ? value->as_geometry() : nullptr;
        if (!geometry || !geometry->geometry) {
            result.failure_message = "Loop requires one canonical geometry input.";
            return result;
        }
        auto options = config.options;
        const auto count = scripting::evaluate_numeric_range(
            config.count,
            frame,
            config.expression_engine.get());
        if (count.error) {
            result.failure_message = *count.error;
            return result;
        }
        options.count = static_cast<std::int64_t>(count.value);
        options.range = -1;
        options.step = -1;
        auto body = config.body;
        body.loop_node_id = frame.inputs.node_id;
        if (body.executor == nullptr) body.executor = frame.executor;
        if (body.function == nullptr) body.function = frame.function;
        body.execution.context = frame.context;
        body.execution.call_stack = frame.call_stack;
        body.execution.element_ids = frame.element_ids;
        GeometryTransactionRequest request;
        request.options = options;
        request.source = *geometry;
        request.owner_actor_id = owner(frame, *geometry);
        request.seed = frame.seed_derivation;
        request.seed.item_key.reset();
        request.body = std::move(body);
        request.item_key = 0;
        request.trace_sink = config.trace_sink;
        request.variables = config.variables;
        auto transaction = run_geometry_transaction(request);
        result.geometry_effects.push_back(transaction.publication);
        if (!transaction.loop.success) {
            result.failures.push_back({frame.inputs.node_id,
                transaction.loop.failed_iteration.value_or(0), transaction.loop.error,
                {{config.geometry_input_port, *value}}, frame.call_stack});
            result.produced_outputs.push_back(
                {config.geometry_output_port, RuntimeValue::geometry_collection({})});
            return result;
        }
        if (transaction.publication.generated_geometry) {
            result.produced_outputs.push_back({config.geometry_output_port,
                RuntimeValue::geometry(transaction.publication.generated_geometry,
                    "loop", request.owner_actor_id)});
        } else {
            result.produced_outputs.push_back(
                {config.geometry_output_port, RuntimeValue::geometry_collection({})});
        }
        return result;
    };
}

std::string configuration_revision(const InstructionConfig& config)
{
    auto revision = configuration_revision(config.options,
        config.body.function ? config.body.function->id : FunctionId{});
    if (config.variables)
        revision += '|' + scripting::variable_configuration_revision(*config.variables);
    const auto engine = config.expression_engine
        ? config.expression_engine
        : std::make_shared<scripting::QuickJsEngine>();
    revision += "|count:" + scripting::numeric_range_revision(config.count, *engine);
    return revision;
}

std::string configuration_revision(const Options& options, const FunctionId& body_function_id)
{
    std::ostringstream stream;
    stream << "loop-v1|" << body_function_id << '|'
           << options.count << '|' << options.range << '|' << options.step << '|'
           << options.hard_iteration_limit << '|' << options.hard_work_limit;
    return stream.str();
}

} // namespace phoenix::loop
