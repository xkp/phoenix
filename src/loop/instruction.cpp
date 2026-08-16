#include "phoenix/loop/instruction.hpp"

#include "phoenix/scripting/quickjs_engine.hpp"

#include <cmath>
#include <limits>
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

std::optional<scripting::Value> scalar(const RuntimeValue& value)
{
    const auto* literal = value.as_literal();
    if (!literal) return std::nullopt;
    const auto first = literal_first_scalar(*literal);
    if (!first) return std::nullopt;
    return std::visit([](const auto& item) -> scripting::Value { return item; }, *first);
}

scripting::Bindings expression_bindings(
    const InstructionExecutionFrame& frame,
    const PortId& geometry_input_port)
{
    scripting::Bindings bindings;
    if (frame.function_variables) {
        bindings = *frame.function_variables;
    }
    for (const auto& input : frame.inputs.promised_inputs) {
        if (input.port == geometry_input_port) continue;
        if (const auto value = scalar(input.value)) bindings[input.port] = *value;
    }
    return bindings;
}

std::optional<std::int64_t> evaluated_integer(
    const scripting::Value& value)
{
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return *integer;
    if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? 1 : 0;
    if (const auto* number = std::get_if<double>(&value)) {
        if (!std::isfinite(*number)) return std::nullopt;
        return static_cast<std::int64_t>(*number);
    }
    return std::nullopt;
}

bool evaluate_option_expression(
    const scripting::Engine& engine,
    const scripting::ExpressionSpec& expression,
    const InstructionExecutionFrame& frame,
    const PortId& geometry_input_port,
    std::int64_t& target,
    std::string& failure)
{
    auto evaluated = scripting::evaluate_expression(
        engine,
        expression,
        expression_bindings(frame, geometry_input_port),
        frame.effective_seed.value_or(frame.context.global_seed));
    if (!evaluated.success()) {
        failure = scripting::format_diagnostics(evaluated);
        return false;
    }
    const auto integer = evaluated_integer(*evaluated.value);
    if (!integer.has_value()) {
        failure = "Loop option expression must evaluate to a numeric scalar.";
        return false;
    }
    target = *integer;
    return true;
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
        if (config.count_expression || config.range_expression || config.step_expression) {
            const auto engine = config.expression_engine
                ? config.expression_engine
                : std::make_shared<scripting::QuickJsEngine>();
            if (config.count_expression && !evaluate_option_expression(
                    *engine,
                    *config.count_expression,
                    frame,
                    config.geometry_input_port,
                    options.count,
                    result.failure_message.emplace())) {
                return result;
            }
            if (config.range_expression && !evaluate_option_expression(
                    *engine,
                    *config.range_expression,
                    frame,
                    config.geometry_input_port,
                    options.range,
                    result.failure_message.emplace())) {
                return result;
            }
            if (config.step_expression && !evaluate_option_expression(
                    *engine,
                    *config.step_expression,
                    frame,
                    config.geometry_input_port,
                    options.step,
                    result.failure_message.emplace())) {
                return result;
            }
        }
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
    if (config.count_expression)
        revision += "|count:" + scripting::expression_configuration_revision(*config.count_expression, *engine);
    if (config.range_expression)
        revision += "|range:" + scripting::expression_configuration_revision(*config.range_expression, *engine);
    if (config.step_expression)
        revision += "|step:" + scripting::expression_configuration_revision(*config.step_expression, *engine);
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
