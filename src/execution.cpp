#include "phoenix/execution.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace phoenix {

namespace {

using RuntimeStateMap = std::unordered_map<NodeId, NodeRuntimeState>;

bool is_output_node(const FunctionDescriptor& function, NodeId node_id) noexcept
{
    return function.output_node_id.has_value() && *function.output_node_id == node_id;
}

const PortDescriptor* find_input_port(
    const InstructionDescriptor& instruction,
    const PortId& port) noexcept
{
    for (const auto& input_port : instruction.input_ports) {
        if (input_port.id == port) {
            return &input_port;
        }
    }

    return nullptr;
}

PortState* find_port_state(NodeRuntimeState& state, const PortId& port) noexcept
{
    for (auto& input_port : state.input_ports) {
        if (input_port.port == port) {
            return &input_port;
        }
    }

    return nullptr;
}

void append_geometry_contributions(
    std::vector<GeometryValue>& contributions,
    const RuntimeValue& value)
{
    if (const auto* geometry = value.as_geometry()) {
        contributions.push_back(*geometry);
        return;
    }

    if (const auto* collection = value.as_geometry_collection()) {
        contributions.insert(
            contributions.end(),
            collection->contributions.begin(),
            collection->contributions.end());
    }
}

std::optional<RuntimeValue> merge_geometry_contributions(
    const RuntimeValue& current,
    const RuntimeValue& incoming)
{
    if ((!current.is_geometry() && !current.is_geometry_collection())
        || (!incoming.is_geometry() && !incoming.is_geometry_collection())) {
        return std::nullopt;
    }

    std::vector<GeometryValue> contributions;
    append_geometry_contributions(contributions, current);
    append_geometry_contributions(contributions, incoming);
    return RuntimeValue::geometry_collection(std::move(contributions));
}

void receive_input(NodeRuntimeState& state, const PortId& port, const RuntimeValue& value)
{
    if (value.is_missing()) {
        return;
    }

    auto received = state.received_inputs.find(port);
    if (received == state.received_inputs.end()) {
        received = state.received_inputs.emplace(port, value).first;
    } else if (auto merged = merge_geometry_contributions(received->second, value)) {
        received->second = *merged;
    } else {
        received->second = value;
    }

    auto* port_state = find_port_state(state, port);
    if (port_state != nullptr) {
        port_state->value = received->second;
    }
}

std::unordered_map<PortId, RuntimeValue> make_input_lookup(const std::vector<PortValue>& inputs)
{
    std::unordered_map<PortId, RuntimeValue> lookup;
    for (const auto& input : inputs) {
        if (!input.value.is_missing()) {
            lookup[input.port] = input.value;
        }
    }

    return lookup;
}

std::unordered_set<PortId> default_ports_for(
    NodeId node_id,
    const std::unordered_map<NodeId, std::vector<PortValue>>& input_defaults)
{
    std::unordered_set<PortId> defaults;
    const auto defaults_it = input_defaults.find(node_id);
    if (defaults_it == input_defaults.end()) {
        return defaults;
    }

    for (const auto& port_default : defaults_it->second) {
        defaults.insert(port_default.port);
    }

    return defaults;
}

std::unordered_set<PortId> promised_port_ids_for(
    const FunctionDescriptor& function,
    const GraphIndex& index,
    const InstructionDescriptor& instruction,
    const std::unordered_map<PortId, RuntimeValue>& function_inputs,
    const std::unordered_map<NodeId, std::vector<PortValue>>& input_defaults)
{
    std::unordered_set<PortId> promised;

    for (const auto* edge : index.incoming_edges(instruction.id)) {
        promised.insert(edge->to_port);
    }

    for (const auto& input_port : instruction.input_ports) {
        if (index.incoming_edges(instruction.id).empty()
            && function_inputs.find(input_port.id) != function_inputs.end()
            && !is_output_node(function, instruction.id)) {
            promised.insert(input_port.id);
        }
    }

    for (const auto& port : default_ports_for(instruction.id, input_defaults)) {
        if (find_input_port(instruction, port) != nullptr) {
            promised.insert(port);
        }
    }

    return promised;
}

bool all_promised_fulfilled(const NodeRuntimeState& state)
{
    for (const auto& port : state.input_ports) {
        if (port.is_promised() && !port.is_fulfilled()) {
            return false;
        }
    }

    return true;
}

bool any_promised_fulfilled(const NodeRuntimeState& state)
{
    for (const auto& port : state.input_ports) {
        if (port.is_promised() && port.is_fulfilled()) {
            return true;
        }
    }

    return false;
}

void refresh_instruction_states(
    const FunctionDescriptor& function,
    RuntimeStateMap& states)
{
    for (const auto& instruction : function.instructions) {
        auto& state = states[instruction.id];
        if (state.state == InstructionState::completed
            || state.state == InstructionState::executing
            || is_output_node(function, instruction.id)) {
            continue;
        }

        if (all_promised_fulfilled(state)) {
            state.state = InstructionState::ready;
        } else if (any_promised_fulfilled(state)) {
            state.state = InstructionState::pending;
        } else {
            state.state = InstructionState::idle;
        }
    }
}

std::vector<NodeId> ready_nodes(const FunctionDescriptor& function, const RuntimeStateMap& states)
{
    std::vector<NodeId> ready;
    for (const auto& instruction : function.instructions) {
        const auto it = states.find(instruction.id);
        if (it != states.end() && it->second.state == InstructionState::ready) {
            ready.push_back(instruction.id);
        }
    }

    std::sort(ready.begin(), ready.end());
    return ready;
}

bool has_pending_predecessor(
    NodeId node_id,
    const GraphIndex& index,
    const RuntimeStateMap& states)
{
    for (const auto* edge : index.incoming_edges(node_id)) {
        const auto predecessor = states.find(edge->from_node);
        if (predecessor != states.end()
            && predecessor->second.state == InstructionState::pending) {
            return true;
        }
    }

    return false;
}

std::optional<NodeId> force_run_candidate(
    const FunctionDescriptor& function,
    const GraphIndex& index,
    const RuntimeStateMap& states)
{
    std::vector<NodeId> candidates;

    for (const auto& instruction : function.instructions) {
        const auto it = states.find(instruction.id);
        if (it != states.end()
            && it->second.state == InstructionState::pending
            && !has_pending_predecessor(instruction.id, index, states)) {
            candidates.push_back(instruction.id);
        }
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    return *std::min_element(candidates.begin(), candidates.end());
}

std::optional<RuntimeValue> find_default_value(
    NodeId node_id,
    const PortId& port,
    const std::unordered_map<NodeId, std::vector<PortValue>>& input_defaults)
{
    const auto defaults_it = input_defaults.find(node_id);
    if (defaults_it == input_defaults.end()) {
        return std::nullopt;
    }

    for (const auto& port_default : defaults_it->second) {
        if (port_default.port == port && !port_default.value.is_missing()) {
            return port_default.value;
        }
    }

    return std::nullopt;
}

InstructionInputs make_instruction_inputs(
    const NodeRuntimeState& state,
    const std::unordered_map<NodeId, std::vector<PortValue>>& input_defaults,
    bool include_unfulfilled_promises)
{
    InstructionInputs inputs;
    inputs.node_id = state.node_id;

    for (const auto& input_port : state.input_ports) {
        if (!input_port.is_promised()) {
            continue;
        }

        RuntimeValue value = input_port.value;
        if (value.is_missing()) {
            const auto default_value = find_default_value(state.node_id, input_port.port, input_defaults);
            if (default_value.has_value()) {
                value = *default_value;
            }
        }

        if (!value.is_missing() || include_unfulfilled_promises) {
            inputs.promised_inputs.push_back(PortValue{input_port.port, value});
        }
    }

    std::sort(
        inputs.promised_inputs.begin(),
        inputs.promised_inputs.end(),
        [](const PortValue& left, const PortValue& right) {
            return left.port < right.port;
        });

    return inputs;
}

FunctionCallPath child_call_path(
    const ExecutionContext& parent_context,
    NodeId caller_node_id,
    const FunctionId& child_function_id)
{
    FunctionCallPath path = parent_context.call_path;
    std::ostringstream segment;
    segment << caller_node_id << ":" << child_function_id;
    path.push_back(segment.str());
    return path;
}

std::vector<PortValue> outputs_as_instruction_outputs(
    NodeId node_id,
    const std::vector<PortValue>& function_outputs)
{
    std::vector<PortValue> outputs;
    outputs.reserve(function_outputs.size());

    for (const auto& function_output : function_outputs) {
        outputs.push_back(PortValue{function_output.port, function_output.value});
    }

    if (outputs.empty()) {
        return outputs;
    }

    InstructionResult result;
    result.node_id = node_id;
    result.produced_outputs = outputs;
    return result.produced_outputs;
}

RuntimeValue failure_context_value(const InstructionFailure& failure)
{
    if (!failure.input_context.empty()) {
        return failure.input_context.front().value;
    }

    return RuntimeValue::literal(LiteralValue{LiteralScalar{failure.message}});
}

bool has_else_route(const GraphIndex& index, NodeId node_id) noexcept
{
    for (const auto* edge : index.outgoing_edges(node_id)) {
        if (edge->from_port == "else") {
            return true;
        }
    }

    return false;
}

void normalize_failures(InstructionResult& result, const InstructionExecutionFrame& frame)
{
    for (auto& failure : result.failures) {
        if (failure.node_id == 0) {
            failure.node_id = result.node_id;
        }
        if (failure.input_context.empty()) {
            failure.input_context = frame.inputs.promised_inputs;
        }
        if (failure.call_stack.empty()) {
            failure.call_stack = frame.call_stack;
        }
    }

    if (result.failure_message.has_value()) {
        result.failures.push_back(InstructionFailure{
            result.node_id,
            std::nullopt,
            *result.failure_message,
            frame.inputs.promised_inputs,
            frame.call_stack,
        });
    }
}

std::vector<InstructionFailure> unhandled_failures(
    const GraphIndex& index,
    const InstructionDescriptor& instruction,
    const InstructionResult& result)
{
    if (result.failures.empty()
        || has_else_route(index, result.node_id)
        || !instruction.failure_is_critical) {
        return {};
    }

    return result.failures;
}

void emit_else_outputs_for_failures(
    const GraphIndex& index,
    const InstructionResult& result,
    RuntimeStateMap& states)
{
    if (result.failures.empty() || !has_else_route(index, result.node_id)) {
        return;
    }

    for (const auto& failure : result.failures) {
        const PortValue else_output{"else", failure_context_value(failure)};
        for (const auto* edge : index.outgoing_edges(result.node_id)) {
            if (edge->from_port == "else") {
                receive_input(states[edge->to_node], edge->to_port, else_output.value);
            }
        }
    }
}

void propagate_outputs(
    const GraphIndex& index,
    const InstructionResult& result,
    RuntimeStateMap& states)
{
    for (const auto& output : result.produced_outputs) {
        if (output.value.is_missing()) {
            continue;
        }

        for (const auto* edge : index.outgoing_edges(result.node_id)) {
            if (edge->from_port == output.port) {
                receive_input(states[edge->to_node], edge->to_port, output.value);
            }
        }
    }
}

std::vector<PortValue> collect_outputs(
    const FunctionDescriptor& function,
    const RuntimeStateMap& states)
{
    std::vector<PortValue> outputs;

    if (!function.output_node_id.has_value()) {
        return outputs;
    }

    const auto output_state = states.find(*function.output_node_id);
    for (const auto& port : function.output_ports) {
        RuntimeValue value = RuntimeValue::empty();
        if (output_state != states.end()) {
            const auto received = output_state->second.received_inputs.find(port.id);
            if (received != output_state->second.received_inputs.end()
                && !received->second.is_missing()) {
                value = received->second;
            }
        }

        outputs.push_back(PortValue{port.id, value});
    }

    return outputs;
}

std::vector<NodeRuntimeState> ordered_states(const FunctionDescriptor& function, const RuntimeStateMap& states)
{
    std::vector<NodeRuntimeState> ordered;
    ordered.reserve(function.instructions.size());

    for (const auto& instruction : function.instructions) {
        const auto it = states.find(instruction.id);
        if (it != states.end()) {
            ordered.push_back(it->second);
        }
    }

    return ordered;
}

bool output_port_matches(const PortDescriptor& function_output, const InstructionDescriptor& output_node)
{
    const auto* input_port = find_input_port(output_node, function_output.id);
    return input_port != nullptr && input_port->type == function_output.type;
}

std::optional<std::string> validate_execution_shape(const FunctionDescriptor& function, const GraphIndex& index)
{
    if (function.output_node_id.has_value() && index.find_instruction(*function.output_node_id) == nullptr) {
        std::ostringstream stream;
        stream << "Function output node '" << *function.output_node_id << "' does not exist.";
        return stream.str();
    }

    if (function.output_ports.empty()) {
        return std::nullopt;
    }

    if (!function.output_node_id.has_value()) {
        return "Function declares outputs but has no output node.";
    }

    const auto* output_node = index.find_instruction(*function.output_node_id);
    if (output_node == nullptr) {
        std::ostringstream stream;
        stream << "Function output node '" << *function.output_node_id << "' does not exist.";
        return stream.str();
    }

    for (const auto& output_port : function.output_ports) {
        if (!output_port_matches(output_port, *output_node)) {
            std::ostringstream stream;
            stream << "Function output port '" << output_port.id
                   << "' has no matching input on the output node.";
            return stream.str();
        }
    }

    return std::nullopt;
}

bool has_pending_node(const RuntimeStateMap& states)
{
    for (const auto& pair : states) {
        if (pair.second.state == InstructionState::pending) {
            return true;
        }
    }

    return false;
}

std::string format_failure_summary(const std::vector<InstructionFailure>& failures)
{
    if (failures.empty()) {
        return "Instruction failed.";
    }

    std::ostringstream stream;
    stream << failures.size() << " unhandled failure";
    if (failures.size() != 1) {
        stream << 's';
    }
    stream << ". First failure at node '" << failures.front().node_id << "'";
    if (!failures.front().message.empty()) {
        stream << ": " << failures.front().message;
    }
    return stream.str();
}

} // namespace

void CallStack::push(CallFrame frame)
{
    frames_.push_back(std::move(frame));
}

std::optional<CallFrame> CallStack::pop()
{
    if (frames_.empty()) {
        return std::nullopt;
    }

    auto frame = frames_.back();
    frames_.pop_back();
    return frame;
}

const CallFrame* CallStack::current() const noexcept
{
    if (frames_.empty()) {
        return nullptr;
    }

    return &frames_.back();
}

const std::vector<CallFrame>& CallStack::frames() const noexcept
{
    return frames_;
}

bool CallStack::empty() const noexcept
{
    return frames_.empty();
}

std::size_t CallStack::size() const noexcept
{
    return frames_.size();
}

void InstructionRegistry::register_handler(std::string kind, InstructionHandler handler)
{
    handlers_[std::move(kind)] = std::move(handler);
}

const InstructionHandler* InstructionRegistry::find_handler(const std::string& kind) const noexcept
{
    const auto it = handlers_.find(kind);
    if (it == handlers_.end()) {
        return nullptr;
    }

    return &it->second;
}

void FunctionLibrary::register_function(const FunctionDescriptor& function)
{
    functions_[function.id] = &function;
}

const FunctionDescriptor* FunctionLibrary::find_function(const FunctionId& id) const noexcept
{
    const auto it = functions_.find(id);
    if (it == functions_.end()) {
        return nullptr;
    }

    return it->second;
}

FunctionExecutor::FunctionExecutor(const InstructionRegistry& registry)
    : registry_(&registry)
{
}

FunctionExecutor::FunctionExecutor(
    const InstructionRegistry& registry,
    const FunctionLibrary& function_library)
    : registry_(&registry)
    , function_library_(&function_library)
{
}

FunctionExecutionResult FunctionExecutor::run(const FunctionExecutionRequest& request) const
{
    FunctionExecutionResult execution_result;

    if (request.function == nullptr) {
        execution_result.status = FunctionExecutionStatus::invalid_request;
        execution_result.failure_message = "Function execution request has no function.";
        return execution_result;
    }

    const auto& function = *request.function;
    const GraphIndex index(function);
    const auto shape_error = validate_execution_shape(function, index);
    if (shape_error.has_value()) {
        execution_result.status = FunctionExecutionStatus::invalid_graph;
        execution_result.failure_message = shape_error;
        return execution_result;
    }

    const auto function_inputs = make_input_lookup(request.inputs);
    CallStack call_stack = request.call_stack;
    if (call_stack.empty()) {
        call_stack.push(CallFrame{
            function.id,
            request.context.call_path,
            std::nullopt,
            std::nullopt,
        });
    }

    RuntimeStateMap states;
    for (const auto& instruction : function.instructions) {
        NodeRuntimeState state;
        state.node_id = instruction.id;

        const auto promised_ports = promised_port_ids_for(
            function,
            index,
            instruction,
            function_inputs,
            request.input_defaults);

        for (const auto& input_port : instruction.input_ports) {
            PortState port_state;
            port_state.port = input_port.id;
            port_state.expectation = promised_ports.find(input_port.id) == promised_ports.end()
                ? PortExpectation::unpromised
                : PortExpectation::promised;
            state.input_ports.push_back(std::move(port_state));
        }

        states.emplace(instruction.id, std::move(state));
    }

    for (const auto& instruction : function.instructions) {
        if (is_output_node(function, instruction.id) || !index.incoming_edges(instruction.id).empty()) {
            continue;
        }

        auto& state = states[instruction.id];
        for (const auto& input_port : instruction.input_ports) {
            const auto input = function_inputs.find(input_port.id);
            if (input != function_inputs.end()) {
                receive_input(state, input_port.id, input->second);
            }
        }
    }

    while (true) {
        refresh_instruction_states(function, states);

        auto ready = ready_nodes(function, states);
        bool force_running = false;
        if (ready.empty()) {
            const auto forced = force_run_candidate(function, index, states);
            if (!forced.has_value()) {
                if (has_pending_node(states)) {
                    execution_result.status = FunctionExecutionStatus::deadlocked;
                    execution_result.failure_message =
                        "Equilibrium reached with no force-runnable pending instruction.";
                }
                break;
            }

            ready.push_back(*forced);
            states[*forced].state = InstructionState::ready;
            force_running = true;
        }

        const auto node_id = ready.front();
        const auto* instruction = index.find_instruction(node_id);
        if (instruction == nullptr) {
            execution_result.status = FunctionExecutionStatus::invalid_graph;
            execution_result.failure_message = "Ready node is missing from graph index.";
            break;
        }

        auto& state = states[node_id];
        state.state = InstructionState::executing;

        SeedDerivationInput seed_input;
        seed_input.global_seed = request.context.global_seed;
        seed_input.call_path = request.context.call_path;
        seed_input.node_id = node_id;
        seed_input.local_seed = instruction->local_seed;

        InstructionExecutionFrame frame;
        frame.context = request.context;
        frame.call_stack = call_stack;
        frame.inputs = make_instruction_inputs(state, request.input_defaults, force_running);
        frame.effective_seed = seed_deriver_.derive(seed_input);

        InstructionResult instruction_result;
        if (instruction->called_function_id.has_value()) {
            if (function_library_ == nullptr) {
                state.state = InstructionState::completed;
                execution_result.status = FunctionExecutionStatus::invalid_request;
                execution_result.failure_message =
                    "Function call instruction requires a function library.";
                break;
            }

            const auto* child_function = function_library_->find_function(*instruction->called_function_id);
            if (child_function == nullptr) {
                std::ostringstream stream;
                stream << "Function call instruction references missing function '"
                       << *instruction->called_function_id << "'.";
                state.state = InstructionState::completed;
                execution_result.status = FunctionExecutionStatus::invalid_graph;
                execution_result.failure_message = stream.str();
                break;
            }

            const auto path = child_call_path(
                request.context,
                instruction->id,
                *instruction->called_function_id);
            CallStack child_stack = call_stack;
            child_stack.push(CallFrame{
                *instruction->called_function_id,
                path,
                instruction->id,
                call_stack.current() == nullptr ? std::nullopt : call_stack.current()->actor_id,
            });

            FunctionExecutionRequest child_request;
            child_request.function = child_function;
            child_request.inputs = frame.inputs.promised_inputs;
            child_request.context.function_id = *instruction->called_function_id;
            child_request.context.call_path = path;
            child_request.context.global_seed = request.context.global_seed;
            child_request.call_stack = child_stack;

            const auto child_result = run(child_request);
            if (child_result.status != FunctionExecutionStatus::completed) {
                instruction_result.node_id = node_id;
                instruction_result.produced_outputs =
                    outputs_as_instruction_outputs(node_id, child_result.outputs);
                instruction_result.failures = child_result.failures;
                if (instruction_result.failures.empty()) {
                    instruction_result.failures.push_back(InstructionFailure{
                        node_id,
                        std::nullopt,
                        child_result.failure_message.value_or("Nested function call failed."),
                        frame.inputs.promised_inputs,
                        frame.call_stack,
                    });
                    instruction_result.failure_message = child_result.failure_message;
                }
            } else {
                instruction_result.node_id = node_id;
                instruction_result.produced_outputs =
                    outputs_as_instruction_outputs(node_id, child_result.outputs);
            }
        } else {
            const auto* handler = registry_->find_handler(instruction->kind);
            if (handler == nullptr) {
                std::ostringstream stream;
                stream << "No instruction handler registered for kind '" << instruction->kind << "'.";
                state.state = InstructionState::completed;
                execution_result.status = FunctionExecutionStatus::missing_handler;
                execution_result.failure_message = stream.str();
                break;
            }

            instruction_result = (*handler)(frame);
        }
        instruction_result.node_id = node_id;
        normalize_failures(instruction_result, frame);

        state.state = InstructionState::completed;

        propagate_outputs(index, instruction_result, states);
        emit_else_outputs_for_failures(index, instruction_result, states);

        if (!instruction_result.failures.empty()) {
            execution_result.failures.insert(
                execution_result.failures.end(),
                instruction_result.failures.begin(),
                instruction_result.failures.end());
        }

        const auto unhandled = unhandled_failures(index, *instruction, instruction_result);
        if (!unhandled.empty()) {
            execution_result.status = FunctionExecutionStatus::failed;
            execution_result.failure_message = format_failure_summary(unhandled);
            break;
        }
    }

    execution_result.outputs = collect_outputs(function, states);
    execution_result.node_states = ordered_states(function, states);
    return execution_result;
}

std::string to_string(InstructionState state)
{
    switch (state) {
    case InstructionState::idle:
        return "idle";
    case InstructionState::pending:
        return "pending";
    case InstructionState::ready:
        return "ready";
    case InstructionState::executing:
        return "executing";
    case InstructionState::completed:
        return "completed";
    }

    return "unknown";
}

std::string to_string(FunctionExecutionStatus status)
{
    switch (status) {
    case FunctionExecutionStatus::completed:
        return "completed";
    case FunctionExecutionStatus::invalid_request:
        return "invalid_request";
    case FunctionExecutionStatus::invalid_graph:
        return "invalid_graph";
    case FunctionExecutionStatus::missing_handler:
        return "missing_handler";
    case FunctionExecutionStatus::deadlocked:
        return "deadlocked";
    case FunctionExecutionStatus::failed:
        return "failed";
    }

    return "unknown";
}

} // namespace phoenix
