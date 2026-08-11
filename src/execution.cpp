#include "phoenix/execution.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace phoenix {

namespace {

using RuntimeStateMap = std::unordered_map<NodeId, NodeRuntimeState>;

enum class GeometryOwnerCheckStatus {
    ok,
    conflict,
};

struct GeometryOwnerCheck {
    GeometryOwnerCheckStatus status = GeometryOwnerCheckStatus::ok;
    std::optional<ActorId> owner;
};

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

GeometryOwnerCheck geometry_owner_for(const RuntimeValue& value)
{
    std::vector<GeometryValue> contributions;
    append_geometry_contributions(contributions, value);

    GeometryOwnerCheck check;
    for (const auto& contribution : contributions) {
        if (!contribution.accumulation_actor_id.has_value()) {
            continue;
        }

        if (!check.owner.has_value()) {
            check.owner = contribution.accumulation_actor_id;
            continue;
        }

        if (*check.owner != *contribution.accumulation_actor_id) {
            check.status = GeometryOwnerCheckStatus::conflict;
            return check;
        }
    }

    return check;
}

GeometryOwnerCheck geometry_owner_for(const std::vector<PortValue>& values)
{
    GeometryOwnerCheck check;
    for (const auto& value : values) {
        const auto value_owner = geometry_owner_for(value.value);
        if (value_owner.status == GeometryOwnerCheckStatus::conflict) {
            return value_owner;
        }

        if (!value_owner.owner.has_value()) {
            continue;
        }

        if (!check.owner.has_value()) {
            check.owner = value_owner.owner;
            continue;
        }

        if (*check.owner != *value_owner.owner) {
            check.status = GeometryOwnerCheckStatus::conflict;
            return check;
        }
    }

    return check;
}

void assign_geometry_owner(RuntimeValue& value, const ActorId& owner)
{
    if (auto* geometry = std::get_if<GeometryValue>(&value.payload)) {
        if (!geometry->accumulation_actor_id.has_value()) {
            geometry->accumulation_actor_id = owner;
        }
        return;
    }

    auto* collection = std::get_if<GeometryCollectionValue>(&value.payload);
    if (collection == nullptr) {
        return;
    }

    for (auto& contribution : collection->contributions) {
        if (!contribution.accumulation_actor_id.has_value()) {
            contribution.accumulation_actor_id = owner;
        }
    }
}

std::vector<PortValue> assign_geometry_output_owners(
    std::vector<PortValue> outputs,
    const ActorId& output_owner)
{
    for (auto& output : outputs) {
        assign_geometry_owner(output.value, output_owner);
    }

    return outputs;
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
    if (GeometryAggregator{}.aggregate(GeometryAggregationInput{"", contributions}).status
        == GeometryAggregationStatus::owner_conflict) {
        return std::nullopt;
    }

    return RuntimeValue::geometry_collection(std::move(contributions));
}

GeometryOwnerCheckStatus receive_input(
    NodeRuntimeState& state,
    const PortId& port,
    const RuntimeValue& value)
{
    if (value.is_missing()) {
        return GeometryOwnerCheckStatus::ok;
    }

    auto received = state.received_inputs.find(port);
    if (received == state.received_inputs.end()) {
        received = state.received_inputs.emplace(port, value).first;
    } else if (auto merged = merge_geometry_contributions(received->second, value)) {
        received->second = *merged;
    } else {
        if ((received->second.is_geometry() || received->second.is_geometry_collection())
            && (value.is_geometry() || value.is_geometry_collection())) {
            return GeometryOwnerCheckStatus::conflict;
        }
        received->second = value;
    }

    auto* port_state = find_port_state(state, port);
    if (port_state != nullptr) {
        port_state->value = received->second;
    }

    return GeometryOwnerCheckStatus::ok;
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

FunctionCallPath item_call_path(FunctionCallPath path, std::uint64_t item_key)
{
    std::ostringstream segment;
    segment << "item:" << item_key;
    path.push_back(segment.str());
    return path;
}

struct ChildInvocation {
    std::optional<std::uint64_t> item_key;
    std::optional<std::string> instance_key;
    FunctionCallPath call_path;
    std::vector<PortValue> inputs;
};

std::optional<std::string> scalar_instance_key(const LiteralScalar& scalar)
{
    if (const auto* integer = std::get_if<std::int64_t>(&scalar)) {
        return std::to_string(*integer);
    }
    if (const auto* floating_point = std::get_if<double>(&scalar)) {
        std::ostringstream stream;
        stream << *floating_point;
        return stream.str();
    }
    if (const auto* boolean = std::get_if<bool>(&scalar)) {
        return *boolean ? "true" : "false";
    }
    if (const auto* text = std::get_if<std::string>(&scalar)) {
        return *text;
    }

    return std::nullopt;
}

std::optional<std::string> instance_key_from_value(const RuntimeValue& value)
{
    const auto* literal = value.as_literal();
    if (literal == nullptr) {
        return std::nullopt;
    }

    const auto scalar = literal_first_scalar(*literal);
    if (!scalar.has_value()) {
        return std::nullopt;
    }

    return scalar_instance_key(*scalar);
}

std::optional<RuntimeValue> literal_item_value(const RuntimeValue& value, std::size_t item_index)
{
    const auto* literal = value.as_literal();
    if (literal == nullptr) {
        return std::nullopt;
    }

    const auto* array = std::get_if<LiteralArray>(literal);
    if (array == nullptr || item_index >= array->size()) {
        return std::nullopt;
    }

    return RuntimeValue::literal(LiteralValue{(*array)[item_index]});
}

std::optional<std::string> instance_key_from_inputs(const std::vector<PortValue>& inputs)
{
    const auto input_it = std::find_if(
        inputs.begin(),
        inputs.end(),
        [](const PortValue& input) {
            return input.port == "instance_key";
        });
    if (input_it == inputs.end()) {
        return std::nullopt;
    }

    return instance_key_from_value(input_it->value);
}

std::vector<ChildInvocation> child_invocations_for(
    const InstructionDescriptor& instruction,
    const FunctionCallPath& base_call_path,
    const std::vector<PortValue>& inputs)
{
    if (!instruction.multiplexes_input) {
        return {ChildInvocation{
            std::nullopt,
            instance_key_from_inputs(inputs),
            base_call_path,
            inputs,
        }};
    }

    const auto input_it = std::find_if(
        inputs.begin(),
        inputs.end(),
        [](const PortValue& input) {
            return input.port == "input";
        });
    if (input_it == inputs.end() || input_it->value.is_missing()) {
        return {};
    }

    if (const auto* collection = input_it->value.as_geometry_collection()) {
        std::vector<ChildInvocation> invocations;
        invocations.reserve(collection->contributions.size());

        for (std::size_t i = 0; i < collection->contributions.size(); ++i) {
            auto item_inputs = inputs;
            auto item_input = std::find_if(
                item_inputs.begin(),
                item_inputs.end(),
                [](const PortValue& input) {
                    return input.port == "input";
                });
            item_input->value = RuntimeValue::geometry(
                collection->contributions[i].debug_label,
                collection->contributions[i].accumulation_actor_id);
            auto instance_key_input = std::find_if(
                item_inputs.begin(),
                item_inputs.end(),
                [](const PortValue& input) {
                    return input.port == "instance_key";
                });
            if (instance_key_input != item_inputs.end()) {
                const auto item_value = literal_item_value(instance_key_input->value, i);
                if (item_value.has_value()) {
                    instance_key_input->value = *item_value;
                }
            }

            const auto item_key = static_cast<std::uint64_t>(i);
            invocations.push_back(ChildInvocation{
                item_key,
                instance_key_from_inputs(item_inputs),
                item_call_path(base_call_path, item_key),
                std::move(item_inputs),
            });
        }

        return invocations;
    }

    return {ChildInvocation{
        std::uint64_t{0},
        instance_key_from_inputs(inputs),
        item_call_path(base_call_path, 0),
        inputs,
    }};
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

GeometryOwnerCheckStatus emit_else_outputs_for_failures(
    const GraphIndex& index,
    const InstructionResult& result,
    RuntimeStateMap& states)
{
    if (result.failures.empty() || !has_else_route(index, result.node_id)) {
        return GeometryOwnerCheckStatus::ok;
    }

    for (const auto& failure : result.failures) {
        const PortValue else_output{"else", failure_context_value(failure)};
        for (const auto* edge : index.outgoing_edges(result.node_id)) {
            if (edge->from_port == "else") {
                if (receive_input(states[edge->to_node], edge->to_port, else_output.value)
                    == GeometryOwnerCheckStatus::conflict) {
                    return GeometryOwnerCheckStatus::conflict;
                }
            }
        }
    }

    return GeometryOwnerCheckStatus::ok;
}

GeometryOwnerCheckStatus propagate_outputs(
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
                if (receive_input(states[edge->to_node], edge->to_port, output.value)
                    == GeometryOwnerCheckStatus::conflict) {
                    return GeometryOwnerCheckStatus::conflict;
                }
            }
        }
    }

    return GeometryOwnerCheckStatus::ok;
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

std::string actor_id_from_call_path(const FunctionCallPath& call_path)
{
    if (call_path.empty()) {
        return "actor:root";
    }

    std::ostringstream stream;
    stream << "actor";
    for (const auto& segment : call_path) {
        stream << ':' << segment;
    }
    return stream.str();
}

std::string prototype_id_for(
    const FunctionId& function_id,
    const std::string& instance_key)
{
    std::ostringstream stream;
    stream << "prototype:" << function_id << ':' << instance_key;
    return stream.str();
}

void merge_child_actors(ActorNode& target, const ActorNode& source)
{
    target.children.insert(target.children.end(), source.children.begin(), source.children.end());
}

void publish_instruction_cache_entry(
    CacheWriter* cache_writer,
    const CacheIdentity& cache_identity,
    NodeId node_id,
    std::optional<SeedValue> effective_seed,
    const std::vector<PortValue>& outputs)
{
    if (cache_writer == nullptr) {
        return;
    }

    const CacheKeyBuilder key_builder;
    InstructionCacheKeyInput key_input;
    key_input.identity = cache_identity;
    key_input.node_id = node_id;
    key_input.effective_seed = effective_seed;

    InstructionCacheEntry entry;
    entry.key = key_builder.instruction_outputs(key_input);
    entry.outputs = outputs;
    cache_writer->put_instruction(std::move(entry));
}

void publish_function_cache_entries(
    CacheWriter* cache_writer,
    const CacheIdentity& cache_identity,
    const FunctionExecutionResult& result)
{
    if (cache_writer == nullptr || result.status != FunctionExecutionStatus::completed) {
        return;
    }

    const CacheKeyBuilder key_builder;
    FunctionCallCacheEntry function_entry;
    function_entry.key = key_builder.function_call(FunctionCallCacheKeyInput{cache_identity});
    function_entry.outputs = result.outputs;
    function_entry.actor = result.actor;
    cache_writer->put_function_call(std::move(function_entry));

    if (!result.actor.has_value()) {
        return;
    }

    ActorSubtreeCacheKeyInput subtree_key_input;
    subtree_key_input.identity = cache_identity;
    subtree_key_input.actor_id = result.actor->id;

    ActorSubtreeCacheEntry subtree_entry;
    subtree_entry.key = key_builder.actor_subtree(subtree_key_input);
    subtree_entry.actor = *result.actor;
    cache_writer->put_actor_subtree(std::move(subtree_entry));
}

std::optional<FunctionExecutionResult> cached_function_result(
    const CacheStore* cache_store,
    const CacheIdentity& cache_identity)
{
    if (cache_store == nullptr) {
        return std::nullopt;
    }

    const CacheKeyBuilder key_builder;
    const auto cached = cache_store->find_function_call(
        key_builder.function_call(FunctionCallCacheKeyInput{cache_identity}));
    if (!cached.has_value()) {
        return std::nullopt;
    }

    FunctionExecutionResult result;
    result.outputs = cached->outputs;
    result.actor = cached->actor;
    return result;
}

std::optional<std::vector<PortValue>> cached_instruction_outputs(
    const CacheStore* cache_store,
    const CacheIdentity& cache_identity,
    NodeId node_id,
    std::optional<SeedValue> effective_seed)
{
    if (cache_store == nullptr) {
        return std::nullopt;
    }

    const CacheKeyBuilder key_builder;
    InstructionCacheKeyInput key_input;
    key_input.identity = cache_identity;
    key_input.node_id = node_id;
    key_input.effective_seed = effective_seed;

    const auto cached = cache_store->find_instruction(
        key_builder.instruction_outputs(key_input));
    if (!cached.has_value()) {
        return std::nullopt;
    }

    return cached->outputs;
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

bool trace_level_includes(ExecutionTraceLevel actual, ExecutionTraceLevel required) noexcept
{
    return static_cast<int>(actual) >= static_cast<int>(required);
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

SeedValue InstructionExecutionFrame::derive_item_seed(std::uint64_t item_key) const noexcept
{
    if (multiplex_seed_mode == MultiplexSeedMode::one_seed_for_all) {
        if (effective_seed.has_value()) {
            return *effective_seed;
        }

        const SeedDeriver deriver;
        return deriver.derive(seed_derivation);
    }

    SeedDerivationInput item_seed_input = seed_derivation;
    item_seed_input.item_key = item_key;

    const SeedDeriver deriver;
    return deriver.derive(item_seed_input);
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
    const bool top_level_invocation = call_stack.empty();
    const ActorId current_actor_id = top_level_invocation
        ? actor_id_from_call_path(request.context.call_path)
        : call_stack.current()->actor_id.value_or(actor_id_from_call_path(request.context.call_path));
    const CacheIdentity cache_identity = CacheIdentityBuilder{}.identity(CacheIdentityInput{
        &function,
        request.context.call_path,
        request.inputs,
        request.context.global_seed,
    });
    if (auto cached_result = cached_function_result(request.cache_store, cache_identity)) {
        return *cached_result;
    }

    ActorNode actor;
    actor.id = current_actor_id;

    if (call_stack.empty()) {
        call_stack.push(CallFrame{
            function.id,
            request.context.call_path,
            std::nullopt,
            current_actor_id,
        });
    }

    std::optional<std::size_t> current_scope_index = request.parent_scope_index;
    if (request.scope_trace_sink != nullptr
        && trace_level_includes(request.trace_level, ExecutionTraceLevel::scope)) {
        const auto* current_frame = call_stack.current();
        FunctionExecutionScopeRecord scope;
        scope.function_id = function.id;
        scope.function = &function;
        scope.call_path = request.context.call_path;
        scope.actor_id = current_actor_id;
        scope.generates_actor = function.generates_actor;
        if (current_frame != nullptr) {
            scope.caller_node_id = current_frame->caller_node_id;
        }
        scope.parent_scope_index = request.parent_scope_index;
        scope.inputs = request.inputs;
        scope.input_defaults = request.input_defaults;
        scope.global_seed = request.context.global_seed;
        current_scope_index = request.scope_trace_sink->record_scope(std::move(scope));
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
                if (receive_input(state, input_port.id, input->second)
                    == GeometryOwnerCheckStatus::conflict) {
                    execution_result.status = FunctionExecutionStatus::failed;
                    execution_result.failure_message =
                        "Geometry inputs from different actor owners cannot be merged.";
                    break;
                }
            }
        }

        if (execution_result.status != FunctionExecutionStatus::completed) {
            break;
        }
    }

    if (execution_result.status != FunctionExecutionStatus::completed) {
        execution_result.node_states = ordered_states(function, states);
        execution_result.actor = actor;
        return execution_result;
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

        if (request.instruction_trace_sink != nullptr
            && trace_level_includes(request.trace_level, ExecutionTraceLevel::instruction)) {
            request.instruction_trace_sink->record_instruction(FunctionExecutionInstructionRecord{
                function.id,
                request.context.call_path,
                node_id,
                instruction->kind,
                current_actor_id,
            });
        }

        SeedDerivationInput seed_input;
        seed_input.global_seed = request.context.global_seed;
        seed_input.call_path = request.context.call_path;
        seed_input.node_id = node_id;
        seed_input.local_seed = instruction->local_seed;

        InstructionExecutionFrame frame;
        frame.context = request.context;
        frame.call_stack = call_stack;
        frame.inputs = make_instruction_inputs(state, request.input_defaults, force_running);
        frame.seed_derivation = seed_input;
        frame.effective_seed = seed_deriver_.derive(seed_input);
        frame.multiplex_seed_mode = instruction->multiplex_seed_mode;

        InstructionResult instruction_result;
        bool geometry_owner_conflict = false;
        bool instruction_cache_hit = false;
        const auto input_owner_check = geometry_owner_for(frame.inputs.promised_inputs);
        if (input_owner_check.status == GeometryOwnerCheckStatus::conflict) {
            geometry_owner_conflict = true;
            instruction_result.node_id = node_id;
            instruction_result.failures.push_back(InstructionFailure{
                node_id,
                std::nullopt,
                "Geometry inputs from different actor owners cannot be merged.",
                frame.inputs.promised_inputs,
                frame.call_stack,
            });
            instruction_result.failure_message = instruction_result.failures.front().message;
        } else if (auto cached_outputs = cached_instruction_outputs(
                       request.cache_store,
                       cache_identity,
                       node_id,
                       frame.effective_seed)) {
            instruction_cache_hit = true;
            instruction_result.node_id = node_id;
            instruction_result.produced_outputs = *cached_outputs;
        } else if (instruction->called_function_id.has_value()) {
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
            const auto child_invocations = child_invocations_for(
                *instruction,
                path,
                frame.inputs.promised_inputs);

            instruction_result.node_id = node_id;
            std::unordered_map<std::string, ActorPrototypeRef> prototypes_by_instance_key;
            for (const auto& child_invocation : child_invocations) {
                const ActorId child_actor_id = child_function->generates_actor
                    ? actor_id_from_call_path(child_invocation.call_path)
                    : current_actor_id;
                const bool can_instance = instruction->enables_instancing
                    && child_function->generates_actor
                    && child_invocation.instance_key.has_value();
                const auto prototype_it = can_instance
                    ? prototypes_by_instance_key.find(*child_invocation.instance_key)
                    : prototypes_by_instance_key.end();
                if (can_instance && prototype_it != prototypes_by_instance_key.end()) {
                    ActorNode instance_actor;
                    instance_actor.id = child_actor_id;
                    instance_actor.prototype = prototype_it->second;
                    actor.children.push_back(std::move(instance_actor));
                    continue;
                }

                CallStack child_stack = call_stack;
                child_stack.push(CallFrame{
                    *instruction->called_function_id,
                    child_invocation.call_path,
                    instruction->id,
                    child_actor_id,
                });

                FunctionExecutionRequest child_request;
                child_request.function = child_function;
                child_request.inputs = child_invocation.inputs;
                child_request.context.function_id = *instruction->called_function_id;
                child_request.context.call_path = child_invocation.call_path;
                child_request.context.global_seed = request.context.global_seed;
                child_request.call_stack = child_stack;
                child_request.trace_level = request.trace_level;
                child_request.scope_trace_sink = request.scope_trace_sink;
                child_request.instruction_trace_sink = request.instruction_trace_sink;
                child_request.cache_store = request.cache_store;
                child_request.cache_writer = request.cache_writer;
                child_request.parent_scope_index = current_scope_index;

                const auto child_result = run(child_request);
                if (child_result.status == FunctionExecutionStatus::completed
                    && child_result.actor.has_value()) {
                    if (child_function->generates_actor) {
                        auto child_actor = *child_result.actor;
                        if (can_instance) {
                            child_actor.prototype = ActorPrototypeRef{
                                prototype_id_for(child_function->id, *child_invocation.instance_key),
                            };
                            prototypes_by_instance_key.emplace(
                                *child_invocation.instance_key,
                                *child_actor.prototype);
                        }
                        actor.children.push_back(std::move(child_actor));
                    } else {
                        merge_child_actors(actor, *child_result.actor);
                    }
                }

                const auto child_outputs = outputs_as_instruction_outputs(node_id, child_result.outputs);
                instruction_result.produced_outputs.insert(
                    instruction_result.produced_outputs.end(),
                    child_outputs.begin(),
                    child_outputs.end());

                if (child_result.status != FunctionExecutionStatus::completed) {
                    const auto first_new_failure = instruction_result.failures.size();
                    instruction_result.failures.insert(
                        instruction_result.failures.end(),
                        child_result.failures.begin(),
                        child_result.failures.end());
                    if (child_result.failures.empty()) {
                        instruction_result.failures.push_back(InstructionFailure{
                            node_id,
                            child_invocation.item_key,
                            child_result.failure_message.value_or("Nested function call failed."),
                            child_invocation.inputs,
                            frame.call_stack,
                        });
                        instruction_result.failure_message = child_result.failure_message;
                    } else if (child_invocation.item_key.has_value()) {
                        for (auto failure_it = instruction_result.failures.begin() + first_new_failure;
                             failure_it != instruction_result.failures.end();
                             ++failure_it) {
                            auto& failure = *failure_it;
                            if (!failure.item_key.has_value()) {
                                failure.item_key = child_invocation.item_key;
                            }
                        }
                    }
                }
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
        const auto output_owner = function.generates_actor
            ? current_actor_id
            : input_owner_check.owner.value_or(current_actor_id);
        instruction_result.produced_outputs = assign_geometry_output_owners(
            std::move(instruction_result.produced_outputs),
            output_owner);
        normalize_failures(instruction_result, frame);

        state.state = InstructionState::completed;

        if (propagate_outputs(index, instruction_result, states)
            == GeometryOwnerCheckStatus::conflict) {
            geometry_owner_conflict = true;
            instruction_result.failures.push_back(InstructionFailure{
                node_id,
                std::nullopt,
                "Geometry outputs from different actor owners cannot be merged.",
                frame.inputs.promised_inputs,
                frame.call_stack,
            });
            instruction_result.failure_message = instruction_result.failures.back().message;
        }
        if (emit_else_outputs_for_failures(index, instruction_result, states)
            == GeometryOwnerCheckStatus::conflict) {
            geometry_owner_conflict = true;
            instruction_result.failures.push_back(InstructionFailure{
                node_id,
                std::nullopt,
                "Geometry else outputs from different actor owners cannot be merged.",
                frame.inputs.promised_inputs,
                frame.call_stack,
            });
            instruction_result.failure_message = instruction_result.failures.back().message;
        }

        if (!instruction_cache_hit && !geometry_owner_conflict && instruction_result.failures.empty()) {
            publish_instruction_cache_entry(
                request.cache_writer,
                cache_identity,
                node_id,
                frame.effective_seed,
                instruction_result.produced_outputs);
        }

        if (!instruction_result.failures.empty()) {
            execution_result.failures.insert(
                execution_result.failures.end(),
                instruction_result.failures.begin(),
                instruction_result.failures.end());
        }

        const auto unhandled = unhandled_failures(index, *instruction, instruction_result);
        if (geometry_owner_conflict) {
            execution_result.status = FunctionExecutionStatus::failed;
            execution_result.failure_message = instruction_result.failure_message;
            break;
        }

        if (!unhandled.empty()) {
            execution_result.status = FunctionExecutionStatus::failed;
            execution_result.failure_message = format_failure_summary(unhandled);
            break;
        }
    }

    execution_result.outputs = collect_outputs(function, states);
    execution_result.node_states = ordered_states(function, states);
    execution_result.actor = actor;
    publish_function_cache_entries(request.cache_writer, cache_identity, execution_result);
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

std::string to_string(ExecutionTraceLevel level)
{
    switch (level) {
    case ExecutionTraceLevel::none:
        return "none";
    case ExecutionTraceLevel::scope:
        return "scope";
    case ExecutionTraceLevel::instruction:
        return "instruction";
    case ExecutionTraceLevel::item:
        return "item";
    case ExecutionTraceLevel::value:
        return "value";
    }

    return "unknown";
}

} // namespace phoenix
