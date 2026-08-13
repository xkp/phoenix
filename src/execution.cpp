#include "phoenix/execution.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <set>
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
    ++state.received_input_counts[port];

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

std::unordered_map<PortId, std::size_t> promised_port_counts_for(
    const FunctionDescriptor& function,
    const GraphIndex& index,
    const InstructionDescriptor& instruction,
    const std::unordered_map<PortId, RuntimeValue>& function_inputs,
    const std::unordered_map<NodeId, std::vector<PortValue>>& input_defaults)
{
    std::unordered_map<PortId, std::size_t> promised;

    for (const auto* edge : index.incoming_edges(instruction.id)) {
        ++promised[edge->to_port];
    }

    for (const auto& input_port : instruction.input_ports) {
        if (index.incoming_edges(instruction.id).empty()
            && function_inputs.find(input_port.id) != function_inputs.end()
            && !is_output_node(function, instruction.id)) {
            promised[input_port.id] = std::max<std::size_t>(promised[input_port.id], 1);
        }
    }

    for (const auto& port : default_ports_for(instruction.id, input_defaults)) {
        if (find_input_port(instruction, port) != nullptr) {
            promised[port] = std::max<std::size_t>(promised[port], 1);
        }
    }

    return promised;
}

bool all_promised_fulfilled(const NodeRuntimeState& state)
{
    for (const auto& port : state.input_ports) {
        if (!port.is_promised()) {
            continue;
        }

        const auto promised_count = state.promised_input_counts.find(port.port);
        const auto received_count = state.received_input_counts.find(port.port);
        const auto expected = promised_count == state.promised_input_counts.end()
            ? std::size_t{1}
            : promised_count->second;
        const auto received = received_count == state.received_input_counts.end()
            ? std::size_t{0}
            : received_count->second;
        if (received < expected || !port.is_fulfilled()) {
            return false;
        }
    }

    return true;
}

bool any_promised_fulfilled(const NodeRuntimeState& state)
{
    for (const auto& port : state.input_ports) {
        const auto received_count = state.received_input_counts.find(port.port);
        if (port.is_promised()
            && received_count != state.received_input_counts.end()
            && received_count->second > 0
            && port.is_fulfilled()) {
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

class ReadyFrontier {
public:
    void sync(const FunctionDescriptor& function, const RuntimeStateMap& states)
    {
        frontier_.clear();
        for (const auto node_id : ready_nodes(function, states)) {
            frontier_.insert(node_id);
        }
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return frontier_.empty();
    }

    void push(NodeId node_id)
    {
        frontier_.insert(node_id);
    }

    [[nodiscard]] std::optional<NodeId> pop_next()
    {
        if (frontier_.empty()) {
            return std::nullopt;
        }

        const auto it = frontier_.begin();
        const auto node_id = *it;
        frontier_.erase(it);
        return node_id;
    }

    [[nodiscard]] std::vector<NodeId> nodes() const
    {
        return {frontier_.begin(), frontier_.end()};
    }

private:
    std::set<NodeId> frontier_;
};

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

void publish_instruction_cache_entry(
    CacheWriter* cache_writer,
    const CacheIdentity& cache_identity,
    NodeId node_id,
    std::optional<SeedValue> effective_seed,
    const std::vector<PortValue>& outputs,
    const std::vector<GeometryItemEffect>& geometry_effects,
    const std::vector<ActorNode>& actor_children)
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
    entry.geometry_effects = geometry_effects;
    entry.actor_children = actor_children;
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

struct InstructionPublicationInput {
    const GraphIndex* index = nullptr;
    const FunctionDescriptor* function = nullptr;
    const InstructionDescriptor* instruction = nullptr;
    const CacheIdentity* cache_identity = nullptr;
    CacheWriter* cache_writer = nullptr;
    NodeId node_id = 0;
    std::optional<SeedValue> effective_seed;
    bool instruction_cache_hit = false;
    std::size_t actor_child_delta_count = 0;
    const InstructionExecutionFrame* frame = nullptr;
    ExecutionTraceLevel trace_level = ExecutionTraceLevel::none;
    FunctionExecutionPublicationTraceSink* publication_trace_sink = nullptr;
};

struct InstructionPublicationResult {
    bool geometry_owner_conflict = false;
    std::vector<InstructionFailure> unhandled_failures;
};

enum class InstructionWorkStatus {
    completed,
    invalid_request,
    invalid_graph,
    missing_handler,
};

struct InstructionWorkInput {
    const InstructionDescriptor* instruction = nullptr;
    const FunctionDescriptor* function = nullptr;
    const FunctionLibrary* function_library = nullptr;
    const InstructionRegistry* registry = nullptr;
    const FunctionExecutor* executor = nullptr;
    const FunctionExecutionRequest* request = nullptr;
    const CacheIdentity* cache_identity = nullptr;
    std::optional<std::size_t> current_scope_index;
    ActorId current_actor_id;
    GeometryPublicationLedger* publication_ledger = nullptr;
    InstructionExecutionFrame frame;
};

struct InstructionWorkResult {
    InstructionWorkStatus status = InstructionWorkStatus::completed;
    std::optional<std::string> failure_message;
    InstructionResult instruction_result;
    bool instruction_cache_hit = false;
    GeometryOwnerCheck input_owner_check;
    std::vector<ActorNode> actor_children;
    std::size_t multiplex_item_count = 0;
    std::size_t multiplex_prototype_work_count = 0;
    std::size_t multiplex_reused_instance_count = 0;
};

struct MultiplexItemResult {
    std::optional<std::uint64_t> item_key;
    std::optional<std::string> instance_key;
    std::optional<ActorPrototypeRef> prototype_ref;
    std::vector<PortValue> produced_outputs;
    std::vector<InstructionFailure> failures;
    std::optional<std::string> failure_message;
    std::vector<ActorNode> actor_children;
};

struct ClassifiedChildInvocation {
    std::size_t invocation_index = 0;
    bool can_instance = false;
    std::optional<std::size_t> prototype_index;
};

struct PreparedInstructionWork {
    const InstructionDescriptor* instruction = nullptr;
    InstructionExecutionFrame frame;
};

struct CompletedInstructionWork {
    const InstructionDescriptor* instruction = nullptr;
    InstructionExecutionFrame frame;
    InstructionWorkResult result;
    FunctionExecutionMode execution_mode = FunctionExecutionMode::serial;
    bool force_run = false;
    std::uint64_t elapsed_microseconds = 0;
};

struct TimedInstructionWorkResult {
    InstructionWorkResult result;
    std::uint64_t elapsed_microseconds = 0;
};

std::optional<InstructionCacheEntry> cached_instruction_result(
    const CacheStore* cache_store,
    const CacheIdentity& cache_identity,
    NodeId node_id,
    std::optional<SeedValue> effective_seed);

bool trace_level_includes(ExecutionTraceLevel actual, ExecutionTraceLevel required) noexcept;
std::string format_failure_summary(const std::vector<InstructionFailure>& failures);
TimedInstructionWorkResult execute_instruction_work_timed(const InstructionWorkInput& input);
MultiplexItemResult execute_child_invocation_item(
    const InstructionWorkInput& input,
    const FunctionDescriptor& child_function,
    const ChildInvocation& child_invocation,
    const ActorId& child_actor_id,
    bool can_instance,
    const ActorPrototypeRef* prototype_ref);
void merge_child_invocation_item_results(
    InstructionWorkResult& result,
    const std::vector<MultiplexItemResult>& item_results);
std::vector<ClassifiedChildInvocation> classify_child_invocations_for_instancing(
    const InstructionDescriptor& instruction,
    const FunctionDescriptor& child_function,
    const std::vector<ChildInvocation>& child_invocations);
MultiplexItemResult reused_instance_result_from_prototype(
    const InstructionWorkInput& input,
    const ChildInvocation& child_invocation,
    const ActorId& child_actor_id,
    const MultiplexItemResult& prototype_result);

bool can_parallelize_child_invocations(
    const InstructionWorkInput& input,
    const InstructionDescriptor& instruction,
    std::size_t invocation_count) noexcept;

bool is_thread_eligible_instruction(
    const FunctionExecutionRequest& request,
    const FunctionDescriptor& function,
    const InstructionDescriptor& instruction,
    bool force_running) noexcept
{
    return !force_running
        && !function.generates_actor
        && !instruction.generates_actor
        && !instruction.called_function_id.has_value()
        && request.cache_store == nullptr
        && request.cache_writer == nullptr;
}

bool can_parallelize_child_invocations(
    const InstructionWorkInput& input,
    const InstructionDescriptor& instruction,
    std::size_t invocation_count) noexcept
{
    return input.request->options.worker_count > 1
        && invocation_count > 1
        && instruction.multiplexes_input
        && input.request->scope_trace_sink == nullptr
        && input.request->instruction_trace_sink == nullptr
        && input.request->publication_trace_sink == nullptr
        && input.request->cache_store == nullptr
        && input.request->cache_writer == nullptr;
}

InstructionExecutionFrame make_execution_frame(
    const FunctionExecutionRequest& request,
    const CallStack& call_stack,
    const NodeRuntimeState& state,
    const InstructionDescriptor& instruction,
    bool force_running,
    const SeedDeriver& seed_deriver)
{
    SeedDerivationInput seed_input;
    seed_input.global_seed = request.context.global_seed;
    seed_input.call_path = request.context.call_path;
    seed_input.node_id = instruction.id;
    seed_input.local_seed = instruction.local_seed;

    InstructionExecutionFrame frame;
    frame.context = request.context;
    frame.call_stack = call_stack;
    frame.inputs = make_instruction_inputs(state, request.input_defaults, force_running);
    frame.seed_derivation = seed_input;
    frame.effective_seed = seed_deriver.derive(seed_input);
    frame.multiplex_seed_mode = instruction.multiplex_seed_mode;
    frame.element_ids = request.element_ids;
    frame.worker_count = request.options.worker_count;
    return frame;
}

void record_instruction_start(
    const FunctionExecutionRequest& request,
    const FunctionDescriptor& function,
    const InstructionDescriptor& instruction,
    const ActorId& current_actor_id)
{
    if (request.instruction_trace_sink == nullptr
        || !trace_level_includes(request.trace_level, ExecutionTraceLevel::instruction)) {
        return;
    }

    request.instruction_trace_sink->record_instruction(FunctionExecutionInstructionRecord{
        function.id,
        request.context.call_path,
        instruction.id,
        instruction.kind,
        current_actor_id,
    });
}

void apply_work_status(
    FunctionExecutionResult& execution_result,
    InstructionWorkStatus status,
    std::optional<std::string> failure_message)
{
    execution_result.failure_message = std::move(failure_message);
    switch (status) {
    case InstructionWorkStatus::invalid_request:
        execution_result.status = FunctionExecutionStatus::invalid_request;
        break;
    case InstructionWorkStatus::invalid_graph:
        execution_result.status = FunctionExecutionStatus::invalid_graph;
        break;
    case InstructionWorkStatus::missing_handler:
        execution_result.status = FunctionExecutionStatus::missing_handler;
        break;
    case InstructionWorkStatus::completed:
        break;
    }
}

InstructionPublicationResult publish_instruction_execution(
    const InstructionPublicationInput& input,
    InstructionResult& instruction_result,
    RuntimeStateMap& states,
    FunctionExecutionResult& execution_result)
{
    InstructionPublicationResult publication;
    states[input.node_id].state = InstructionState::completed;

    if (propagate_outputs(*input.index, instruction_result, states)
        == GeometryOwnerCheckStatus::conflict) {
        publication.geometry_owner_conflict = true;
        instruction_result.failures.push_back(InstructionFailure{
            input.node_id,
            std::nullopt,
            "Geometry outputs from different actor owners cannot be merged.",
            input.frame->inputs.promised_inputs,
            input.frame->call_stack,
        });
        instruction_result.failure_message = instruction_result.failures.back().message;
    }
    if (emit_else_outputs_for_failures(*input.index, instruction_result, states)
        == GeometryOwnerCheckStatus::conflict) {
        publication.geometry_owner_conflict = true;
        instruction_result.failures.push_back(InstructionFailure{
            input.node_id,
            std::nullopt,
            "Geometry else outputs from different actor owners cannot be merged.",
            input.frame->inputs.promised_inputs,
            input.frame->call_stack,
        });
        instruction_result.failure_message = instruction_result.failures.back().message;
    }

    if (!input.instruction_cache_hit
        && !publication.geometry_owner_conflict
        && instruction_result.failures.empty()) {
        publish_instruction_cache_entry(
            input.cache_writer,
            *input.cache_identity,
            input.node_id,
            input.effective_seed,
            instruction_result.produced_outputs,
            instruction_result.geometry_effects,
            instruction_result.actor_children);
    }

    if (!instruction_result.failures.empty()) {
        execution_result.failures.insert(
            execution_result.failures.end(),
            instruction_result.failures.begin(),
            instruction_result.failures.end());
    }

    publication.unhandled_failures = unhandled_failures(
        *input.index,
        *input.instruction,
        instruction_result);

    if (input.publication_trace_sink != nullptr
        && trace_level_includes(input.trace_level, ExecutionTraceLevel::instruction)) {
        input.publication_trace_sink->record_publication(FunctionExecutionPublicationRecord{
            input.function->id,
            input.frame->context.call_path,
            input.node_id,
            input.frame->call_stack.current() == nullptr
                ? std::nullopt
                : input.frame->call_stack.current()->actor_id,
            instruction_result.produced_outputs.size(),
            instruction_result.failures.size(),
            input.actor_child_delta_count,
            input.instruction_cache_hit,
        });
    }
    return publication;
}

InstructionWorkResult execute_instruction_work(const InstructionWorkInput& input)
{
    InstructionWorkResult result;
    const auto& instruction = *input.instruction;
    const auto& frame = input.frame;
    const auto node_id = instruction.id;

    result.input_owner_check = geometry_owner_for(frame.inputs.promised_inputs);
    if (result.input_owner_check.status == GeometryOwnerCheckStatus::conflict) {
        result.instruction_result.node_id = node_id;
        result.instruction_result.failures.push_back(InstructionFailure{
            node_id,
            std::nullopt,
            "Geometry inputs from different actor owners cannot be merged.",
            frame.inputs.promised_inputs,
            frame.call_stack,
        });
        result.instruction_result.failure_message =
            result.instruction_result.failures.front().message;
        return result;
    }

    if (!instruction.called_function_id.has_value()) {
        if (auto cached_result = cached_instruction_result(
            input.request->cache_store,
            *input.cache_identity,
            node_id,
            frame.effective_seed)) {
            result.instruction_cache_hit = true;
            result.instruction_result.node_id = node_id;
            result.instruction_result.produced_outputs = cached_result->outputs;
            result.instruction_result.geometry_effects = cached_result->geometry_effects;
            result.actor_children = cached_result->actor_children;
            result.instruction_result.actor_children = cached_result->actor_children;
            return result;
        }
    }

    if (instruction.called_function_id.has_value()) {
        if (input.function_library == nullptr) {
            result.status = InstructionWorkStatus::invalid_request;
            result.failure_message = "Function call instruction requires a function library.";
            return result;
        }

        const auto* child_function =
            input.function_library->find_function(*instruction.called_function_id);
        if (child_function == nullptr) {
            std::ostringstream stream;
            stream << "Function call instruction references missing function '"
                   << *instruction.called_function_id << "'.";
            result.status = InstructionWorkStatus::invalid_graph;
            result.failure_message = stream.str();
            return result;
        }

        const auto path = child_call_path(
            input.request->context,
            instruction.id,
            *instruction.called_function_id);
        const auto child_invocations = child_invocations_for(
            instruction,
            path,
            frame.inputs.promised_inputs);

        result.instruction_result.node_id = node_id;
        result.multiplex_item_count = instruction.multiplexes_input
            ? child_invocations.size()
            : std::size_t{0};
        std::unordered_map<std::string, ActorPrototypeRef> prototypes_by_instance_key;
        std::vector<MultiplexItemResult> item_results(child_invocations.size());
        if (can_parallelize_child_invocations(input, instruction, child_invocations.size())) {
            const auto classified_invocations = classify_child_invocations_for_instancing(
                instruction,
                *child_function,
                child_invocations);
            std::vector<std::size_t> work_indexes;
            work_indexes.reserve(classified_invocations.size());
            for (const auto& classified_invocation : classified_invocations) {
                if (!classified_invocation.prototype_index.has_value()) {
                    work_indexes.push_back(classified_invocation.invocation_index);
                }
            }
            result.multiplex_prototype_work_count = instruction.multiplexes_input
                ? work_indexes.size()
                : std::size_t{0};
            result.multiplex_reused_instance_count = instruction.multiplexes_input
                ? child_invocations.size() - work_indexes.size()
                : std::size_t{0};

            for (std::size_t offset = 0; offset < work_indexes.size();) {
                std::vector<std::pair<std::size_t, std::future<MultiplexItemResult>>> futures;
                const auto batch_size = std::min<std::size_t>(
                    input.request->options.worker_count,
                    work_indexes.size() - offset);
                futures.reserve(batch_size);
                for (std::size_t i = 0; i < batch_size; ++i) {
                    const auto invocation_index = work_indexes[offset + i];
                    const auto& child_invocation = child_invocations[invocation_index];
                    const ActorId child_actor_id = child_function->generates_actor
                        ? actor_id_from_call_path(child_invocation.call_path)
                        : input.current_actor_id;
                    const bool can_instance = instruction.enables_instancing
                        && child_function->generates_actor
                        && child_invocation.instance_key.has_value();
                    futures.emplace_back(
                        invocation_index,
                        std::async(
                            std::launch::async,
                            [input, child_function, child_invocation, child_actor_id, can_instance]() {
                                return execute_child_invocation_item(
                                    input,
                                    *child_function,
                                    child_invocation,
                                    child_actor_id,
                                    can_instance,
                                    nullptr);
                            }));
                }
                for (auto& future : futures) {
                    item_results[future.first] = future.second.get();
                }
                offset += batch_size;
            }

            for (const auto& classified_invocation : classified_invocations) {
                if (!classified_invocation.prototype_index.has_value()) {
                    continue;
                }

                const auto& child_invocation = child_invocations[classified_invocation.invocation_index];
                const auto& prototype_result = item_results[*classified_invocation.prototype_index];
                const ActorId child_actor_id = child_function->generates_actor
                    ? actor_id_from_call_path(child_invocation.call_path)
                    : input.current_actor_id;
                item_results[classified_invocation.invocation_index] =
                    reused_instance_result_from_prototype(
                        input,
                        child_invocation,
                        child_actor_id,
                        prototype_result);
            }
        } else {
            item_results.clear();
            item_results.reserve(child_invocations.size());
            for (const auto& child_invocation : child_invocations) {
                const ActorId child_actor_id = child_function->generates_actor
                    ? actor_id_from_call_path(child_invocation.call_path)
                    : input.current_actor_id;
                const bool can_instance = instruction.enables_instancing
                    && child_function->generates_actor
                    && child_invocation.instance_key.has_value();
                const auto prototype_it = can_instance
                    ? prototypes_by_instance_key.find(*child_invocation.instance_key)
                    : prototypes_by_instance_key.end();
                if (instruction.multiplexes_input) {
                    if (prototype_it == prototypes_by_instance_key.end()) {
                        ++result.multiplex_prototype_work_count;
                    } else {
                        ++result.multiplex_reused_instance_count;
                    }
                }

                auto item_result = execute_child_invocation_item(
                    input,
                    *child_function,
                    child_invocation,
                    child_actor_id,
                    can_instance,
                    prototype_it == prototypes_by_instance_key.end() ? nullptr : &prototype_it->second);
                if (item_result.prototype_ref.has_value() && child_invocation.instance_key.has_value()) {
                    prototypes_by_instance_key.emplace(
                        *child_invocation.instance_key,
                        *item_result.prototype_ref);
                }
                item_results.push_back(std::move(item_result));
            }
        }
        merge_child_invocation_item_results(result, item_results);
        return result;
    }

    const auto* handler = input.registry->find_handler(instruction.kind);
    if (handler == nullptr) {
        std::ostringstream stream;
        stream << "No instruction handler registered for kind '" << instruction.kind << "'.";
        result.status = InstructionWorkStatus::missing_handler;
        result.failure_message = stream.str();
        return result;
    }

    result.instruction_result = (*handler)(frame);
    result.actor_children = result.instruction_result.actor_children;
    return result;
}

void publish_actor_deltas(ActorNode& actor, const InstructionWorkResult& work_result)
{
    actor.children.insert(
        actor.children.end(),
        work_result.actor_children.begin(),
        work_result.actor_children.end());
}

bool commit_completed_instruction_work(
    const GraphIndex& index,
    const FunctionDescriptor& function,
    const CacheIdentity& cache_identity,
    const FunctionExecutionRequest& request,
    const ActorId& current_actor_id,
    CompletedInstructionWork& completed,
    GeometryPublicationLedger& geometry_ledger,
    ActorNode& actor,
    RuntimeStateMap& states,
    FunctionExecutionResult& execution_result)
{
    const auto node_id = completed.instruction->id;
    auto& state = states[node_id];
    if (completed.result.status != InstructionWorkStatus::completed) {
        state.state = InstructionState::completed;
        apply_work_status(
            execution_result,
            completed.result.status,
            completed.result.failure_message);
        return false;
    }

    publish_actor_deltas(actor, completed.result);

    auto& instruction_result = completed.result.instruction_result;
    instruction_result.node_id = node_id;
    const auto output_owner = function.generates_actor
        ? current_actor_id
        : completed.result.input_owner_check.owner.value_or(current_actor_id);
    instruction_result.produced_outputs = assign_geometry_output_owners(
        std::move(instruction_result.produced_outputs),
        output_owner);
    normalize_failures(instruction_result, completed.frame);

    const auto geometry_publication = geometry_ledger.replace_scope(
        PublicationScopeKey{function.id, completed.frame.context.call_path, node_id},
        current_actor_id,
        completed.instruction->consumes_geometry,
        instruction_result.geometry_effects);
    for (const auto& item : geometry_publication.diagnostics) {
        if (item.code != PublicationDiagnosticCode::failed_item_attempted_consumption) continue;
        instruction_result.failures.push_back(InstructionFailure{
            node_id,
            item.item_key,
            item.message,
            completed.frame.inputs.promised_inputs,
            completed.frame.call_stack,
        });
    }

    const auto publication = publish_instruction_execution(
        InstructionPublicationInput{
            &index,
            &function,
            completed.instruction,
            &cache_identity,
            request.cache_writer,
            node_id,
            completed.frame.effective_seed,
            completed.result.instruction_cache_hit,
            completed.result.actor_children.size(),
            &completed.frame,
            request.trace_level,
            request.publication_trace_sink,
        },
        instruction_result,
        states,
        execution_result);

    if (request.diagnostics_sink != nullptr) {
        request.diagnostics_sink->record_diagnostics(FunctionExecutionDiagnosticsRecord{
            function.id,
            completed.frame.context.call_path,
            node_id,
            completed.instruction->kind,
            completed.frame.call_stack.current() == nullptr
                ? std::nullopt
                : completed.frame.call_stack.current()->actor_id,
            completed.execution_mode,
            completed.force_run,
            request.options.worker_count,
            completed.elapsed_microseconds,
            instruction_result.produced_outputs.size(),
            instruction_result.failures.size(),
            completed.result.actor_children.size(),
            completed.result.instruction_cache_hit,
            completed.result.multiplex_item_count,
            completed.result.multiplex_prototype_work_count,
            completed.result.multiplex_reused_instance_count,
        });
    }

    if (publication.geometry_owner_conflict) {
        execution_result.status = FunctionExecutionStatus::failed;
        execution_result.failure_message = instruction_result.failure_message;
        return false;
    }

    if (!publication.unhandled_failures.empty()) {
        execution_result.status = FunctionExecutionStatus::failed;
        execution_result.failure_message =
            format_failure_summary(publication.unhandled_failures);
        return false;
    }

    return true;
}

TimedInstructionWorkResult execute_instruction_work_timed(const InstructionWorkInput& input)
{
    const auto started_at = std::chrono::steady_clock::now();
    TimedInstructionWorkResult timed;
    timed.result = execute_instruction_work(input);
    const auto finished_at = std::chrono::steady_clock::now();
    timed.elapsed_microseconds =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            finished_at - started_at).count());
    return timed;
}

std::vector<ClassifiedChildInvocation> classify_child_invocations_for_instancing(
    const InstructionDescriptor& instruction,
    const FunctionDescriptor& child_function,
    const std::vector<ChildInvocation>& child_invocations)
{
    std::vector<ClassifiedChildInvocation> classified_invocations;
    classified_invocations.reserve(child_invocations.size());

    std::unordered_map<std::string, std::size_t> prototype_indexes;
    for (std::size_t i = 0; i < child_invocations.size(); ++i) {
        const auto& child_invocation = child_invocations[i];
        ClassifiedChildInvocation classified_invocation;
        classified_invocation.invocation_index = i;
        classified_invocation.can_instance = instruction.enables_instancing
            && child_function.generates_actor
            && child_invocation.instance_key.has_value();

        if (classified_invocation.can_instance) {
            const auto [prototype_it, inserted] =
                prototype_indexes.emplace(*child_invocation.instance_key, i);
            if (!inserted) {
                classified_invocation.prototype_index = prototype_it->second;
            }
        }

        classified_invocations.push_back(classified_invocation);
    }

    return classified_invocations;
}

MultiplexItemResult reused_instance_result_from_prototype(
    const InstructionWorkInput& input,
    const ChildInvocation& child_invocation,
    const ActorId& child_actor_id,
    const MultiplexItemResult& prototype_result)
{
    MultiplexItemResult item_result;
    item_result.item_key = child_invocation.item_key;
    item_result.instance_key = child_invocation.instance_key;

    if (prototype_result.prototype_ref.has_value()) {
        ActorNode instance_actor;
        instance_actor.id = child_actor_id;
        instance_actor.prototype = *prototype_result.prototype_ref;
        item_result.actor_children.push_back(std::move(instance_actor));
        return item_result;
    }

    item_result.failures = prototype_result.failures;
    for (auto& failure : item_result.failures) {
        failure.node_id = input.instruction->id;
        failure.item_key = child_invocation.item_key;
        failure.input_context = child_invocation.inputs;
        failure.call_stack = input.frame.call_stack;
    }
    item_result.failure_message = prototype_result.failure_message;
    if (item_result.failures.empty()) {
        item_result.failures.push_back(InstructionFailure{
            input.instruction->id,
            child_invocation.item_key,
            item_result.failure_message.value_or("Instanced prototype item failed."),
            child_invocation.inputs,
            input.frame.call_stack,
        });
    }

    return item_result;
}

MultiplexItemResult execute_child_invocation_item(
    const InstructionWorkInput& input,
    const FunctionDescriptor& child_function,
    const ChildInvocation& child_invocation,
    const ActorId& child_actor_id,
    bool can_instance,
    const ActorPrototypeRef* prototype_ref)
{
    MultiplexItemResult item_result;
    item_result.item_key = child_invocation.item_key;
    item_result.instance_key = child_invocation.instance_key;

    if (can_instance && prototype_ref != nullptr) {
        ActorNode instance_actor;
        instance_actor.id = child_actor_id;
        instance_actor.prototype = *prototype_ref;
        item_result.actor_children.push_back(std::move(instance_actor));
        return item_result;
    }

    CallStack child_stack = input.frame.call_stack;
    child_stack.push(CallFrame{
        *input.instruction->called_function_id,
        child_invocation.call_path,
        input.instruction->id,
        child_actor_id,
    });

    FunctionExecutionRequest child_request;
    child_request.function = &child_function;
    child_request.inputs = child_invocation.inputs;
    child_request.context.function_id = *input.instruction->called_function_id;
    child_request.context.call_path = child_invocation.call_path;
    child_request.context.global_seed = input.request->context.global_seed;
    child_request.call_stack = child_stack;
    child_request.trace_level = input.request->trace_level;
    child_request.scope_trace_sink = input.request->scope_trace_sink;
    child_request.instruction_trace_sink = input.request->instruction_trace_sink;
    child_request.publication_trace_sink = input.request->publication_trace_sink;
    child_request.diagnostics_sink = input.request->diagnostics_sink;
    child_request.options = input.request->options;
    child_request.cache_store = input.request->cache_store;
    child_request.cache_writer = input.request->cache_writer;
    child_request.parent_scope_index = input.current_scope_index;
    child_request.publication_ledger = input.publication_ledger;
    child_request.element_ids = input.request->element_ids;

    const auto child_result = input.executor->run(child_request);
    if (child_result.status == FunctionExecutionStatus::completed
        && child_result.actor.has_value()) {
        if (child_function.generates_actor) {
            auto child_actor = *child_result.actor;
            if (can_instance) {
                child_actor.prototype = ActorPrototypeRef{
                    prototype_id_for(child_function.id, *child_invocation.instance_key),
                };
                item_result.prototype_ref = child_actor.prototype;
            }
            item_result.actor_children.push_back(std::move(child_actor));
        } else {
            item_result.actor_children.insert(
                item_result.actor_children.end(),
                child_result.actor->children.begin(),
                child_result.actor->children.end());
        }
    }

    item_result.produced_outputs = outputs_as_instruction_outputs(
        input.instruction->id,
        child_result.outputs);

    if (child_result.status != FunctionExecutionStatus::completed) {
        item_result.failures = child_result.failures;
        if (child_result.failures.empty()) {
            item_result.failures.push_back(InstructionFailure{
                input.instruction->id,
                child_invocation.item_key,
                child_result.failure_message.value_or("Nested function call failed."),
                child_invocation.inputs,
                input.frame.call_stack,
            });
            item_result.failure_message = child_result.failure_message;
        } else if (child_invocation.item_key.has_value()) {
            for (auto& failure : item_result.failures) {
                if (!failure.item_key.has_value()) {
                    failure.item_key = child_invocation.item_key;
                }
            }
        }
    }

    return item_result;
}

void merge_child_invocation_item_results(
    InstructionWorkResult& result,
    const std::vector<MultiplexItemResult>& item_results)
{
    for (const auto& item_result : item_results) {
        result.actor_children.insert(
            result.actor_children.end(),
            item_result.actor_children.begin(),
            item_result.actor_children.end());
        result.instruction_result.produced_outputs.insert(
            result.instruction_result.produced_outputs.end(),
            item_result.produced_outputs.begin(),
            item_result.produced_outputs.end());
        result.instruction_result.failures.insert(
            result.instruction_result.failures.end(),
            item_result.failures.begin(),
            item_result.failures.end());
        if (item_result.failure_message.has_value()) {
            result.instruction_result.failure_message = item_result.failure_message;
        }
    }
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

std::optional<InstructionCacheEntry> cached_instruction_result(
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

    return cached;
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

FunctionExecutionResult FunctionExecutor::run(const FunctionExecutionRequest& request_) const
{
    RunElementIdAllocator local_element_ids;
    FunctionExecutionRequest request_storage = request_;
    if (request_storage.element_ids == nullptr) {
        auto reserve_geometry_ids = [&](const CanonicalGeometryRef& geometry) {
            if (!geometry) return;
            for (const auto& vertex : geometry->vertices())
                if (vertex.id.valid()) local_element_ids.advance_past(vertex.id.value());
            for (const auto& halfedge : geometry->halfedges()) {
                if (halfedge.id.valid()) local_element_ids.advance_past(halfedge.id.value());
                if (halfedge.edge_id.valid()) local_element_ids.advance_past(halfedge.edge_id.value());
            }
            for (const auto& face : geometry->faces())
                if (face.id.valid()) local_element_ids.advance_past(face.id.value());
        };
        for (const auto& input : request_storage.inputs) {
            if (const auto* geometry = input.value.as_geometry()) {
                reserve_geometry_ids(geometry->geometry);
            } else if (const auto* collection = input.value.as_geometry_collection()) {
                for (const auto& contribution : collection->contributions)
                    reserve_geometry_ids(contribution.geometry);
            }
        }
        request_storage.element_ids = &local_element_ids;
    }
    const auto& request = request_storage;
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
        request.label_registry_fingerprint,
        request.kernel_version,
        request.adapter_version,
        request.repair_policy_version,
    });
    const bool function_consumes_geometry = std::any_of(
        function.instructions.begin(), function.instructions.end(),
        [](const InstructionDescriptor& instruction) {
            return instruction.consumes_geometry;
        });
    if (request.publication_ledger == nullptr || !function_consumes_geometry) {
        if (auto cached_result = cached_function_result(request.cache_store, cache_identity)) {
            return *cached_result;
        }
    }

    GeometryPublicationLedger local_geometry_ledger;
    auto* geometry_ledger = request.publication_ledger == nullptr
        ? &local_geometry_ledger
        : request.publication_ledger;

    ActorNode actor;
    actor.id = current_actor_id;
    for (const auto& input : request.inputs) {
        const auto* geometry = input.value.as_geometry();
        if (geometry != nullptr && geometry->geometry != nullptr
            && geometry->accumulation_actor_id.value_or(current_actor_id) == current_actor_id) {
            geometry_ledger->set_actor_source(current_actor_id, geometry->geometry);
        }
    }

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

        const auto promised_ports = promised_port_counts_for(
            function,
            index,
            instruction,
            function_inputs,
            request.input_defaults);
        state.promised_input_counts = promised_ports;

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

    ReadyFrontier frontier;

    while (true) {
        refresh_instruction_states(function, states);
        frontier.sync(function, states);

        bool force_running = false;
        if (frontier.empty()) {
            const auto forced = force_run_candidate(function, index, states);
            if (!forced.has_value()) {
                if (has_pending_node(states)) {
                    execution_result.status = FunctionExecutionStatus::deadlocked;
                    execution_result.failure_message =
                        "Equilibrium reached with no force-runnable pending instruction.";
                }
                break;
            }

            states[*forced].state = InstructionState::ready;
            frontier.push(*forced);
            force_running = true;
        }

        std::vector<CompletedInstructionWork> completed_batch;
        if (request.options.worker_count > 1 && !force_running) {
            std::vector<const InstructionDescriptor*> eligible;
            for (const auto ready_node_id : frontier.nodes()) {
                const auto* ready_instruction = index.find_instruction(ready_node_id);
                if (ready_instruction == nullptr) {
                    execution_result.status = FunctionExecutionStatus::invalid_graph;
                    execution_result.failure_message = "Ready node is missing from graph index.";
                    break;
                }
                if (!is_thread_eligible_instruction(
                        request,
                        function,
                        *ready_instruction,
                        force_running)) {
                    continue;
                }

                eligible.push_back(ready_instruction);

                if (eligible.size() >= request.options.worker_count) {
                    break;
                }
            }

            if (execution_result.status != FunctionExecutionStatus::completed) {
                break;
            }

            std::vector<PreparedInstructionWork> prepared;
            prepared.reserve(eligible.size());
            if (eligible.size() > 1) {
                for (const auto* ready_instruction : eligible) {
                    auto& ready_state = states[ready_instruction->id];
                    ready_state.state = InstructionState::executing;
                    record_instruction_start(request, function, *ready_instruction, current_actor_id);
                    prepared.push_back(PreparedInstructionWork{
                        ready_instruction,
                        make_execution_frame(
                            request,
                            call_stack,
                            ready_state,
                            *ready_instruction,
                            force_running,
                            seed_deriver_),
                    });
                }

                std::vector<std::future<TimedInstructionWorkResult>> futures;
                futures.reserve(prepared.size());
                for (const auto& work : prepared) {
                    auto work_input = InstructionWorkInput{
                        work.instruction,
                        &function,
                        function_library_,
                        registry_,
                        this,
                        &request,
                        &cache_identity,
                        current_scope_index,
                        current_actor_id,
                        geometry_ledger,
                        work.frame,
                    };
                    futures.push_back(std::async(
                        std::launch::async,
                        [work_input]() {
                            return execute_instruction_work_timed(work_input);
                        }));
                }

                completed_batch.reserve(prepared.size());
                for (std::size_t i = 0; i < prepared.size(); ++i) {
                    auto timed_result = futures[i].get();
                    completed_batch.push_back(CompletedInstructionWork{
                        prepared[i].instruction,
                        prepared[i].frame,
                        std::move(timed_result.result),
                        FunctionExecutionMode::worker,
                        false,
                        timed_result.elapsed_microseconds,
                    });
                }
            }
        }

        if (completed_batch.empty()) {
            const auto next_node_id = frontier.pop_next();
            if (!next_node_id.has_value()) {
                execution_result.status = FunctionExecutionStatus::deadlocked;
                execution_result.failure_message = "Ready frontier produced no executable node.";
                break;
            }

            const auto node_id = *next_node_id;
            const auto* instruction = index.find_instruction(node_id);
            if (instruction == nullptr) {
                execution_result.status = FunctionExecutionStatus::invalid_graph;
                execution_result.failure_message = "Ready node is missing from graph index.";
                break;
            }

            auto& state = states[node_id];
            state.state = InstructionState::executing;
            record_instruction_start(request, function, *instruction, current_actor_id);

            auto frame = make_execution_frame(
                request,
                call_stack,
                state,
                *instruction,
                force_running,
                seed_deriver_);

            auto timed_result = execute_instruction_work_timed(InstructionWorkInput{
                    instruction,
                    &function,
                    function_library_,
                    registry_,
                    this,
                    &request,
                    &cache_identity,
                    current_scope_index,
                    current_actor_id,
                    geometry_ledger,
                    frame,
                });
            completed_batch.push_back(CompletedInstructionWork{
                instruction,
                frame,
                std::move(timed_result.result),
                FunctionExecutionMode::serial,
                force_running,
                timed_result.elapsed_microseconds,
            });
        }

        std::sort(
            completed_batch.begin(),
            completed_batch.end(),
            [](const CompletedInstructionWork& left, const CompletedInstructionWork& right) {
                return left.instruction->id < right.instruction->id;
            });

        for (auto& completed : completed_batch) {
            if (!commit_completed_instruction_work(
                    index,
                    function,
                    cache_identity,
                    request,
                    current_actor_id,
                    completed,
                    *geometry_ledger,
                    actor,
                    states,
                    execution_result)) {
                break;
            }
        }

        if (execution_result.status != FunctionExecutionStatus::completed) {
            break;
        }
    }

    execution_result.outputs = collect_outputs(function, states);
    if (const auto assembled = geometry_ledger->assemble_actor(current_actor_id);
        assembled != nullptr && !assembled->faces().empty()) {
        actor.geometry = GeometryValue{"published", current_actor_id, assembled};
    }
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
