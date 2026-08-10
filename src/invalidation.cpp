#include "phoenix/invalidation.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace phoenix {

namespace {

std::vector<NodeId> sorted_nodes(const std::unordered_set<NodeId>& nodes)
{
    std::vector<NodeId> ordered(nodes.begin(), nodes.end());
    std::sort(ordered.begin(), ordered.end());
    return ordered;
}

bool contains_node(const std::unordered_set<NodeId>& nodes, NodeId node_id) noexcept
{
    return nodes.find(node_id) != nodes.end();
}

bool dirty_actor_instruction_exists(
    const FunctionDescriptor& function,
    const std::unordered_set<NodeId>& dirty)
{
    for (const auto& instruction : function.instructions) {
        if (instruction.generates_actor && contains_node(dirty, instruction.id)) {
            return true;
        }
    }

    return false;
}

void add_reason(std::vector<InvalidationReason>& reasons, InvalidationReason reason)
{
    reasons.push_back(reason);
}

} // namespace

InvalidationResult InvalidationPlanner::plan(const InvalidationRequest& request) const
{
    InvalidationResult result;
    if (request.function == nullptr) {
        return result;
    }

    const auto& function = *request.function;
    const GraphIndex index(function);

    std::unordered_set<NodeId> dirty;
    std::queue<NodeId> pending;
    for (const auto node_id : request.changed_instructions) {
        if (index.find_instruction(node_id) == nullptr) {
            continue;
        }

        const auto inserted = dirty.insert(node_id);
        if (inserted.second) {
            pending.push(node_id);
        }
    }

    while (!pending.empty()) {
        const auto node_id = pending.front();
        pending.pop();

        for (const auto* edge : index.outgoing_edges(node_id)) {
            const auto inserted = dirty.insert(edge->to_node);
            if (inserted.second) {
                pending.push(edge->to_node);
            }
        }
    }

    result.dirty_instructions = sorted_nodes(dirty);
    result.function_outputs_affected = function.output_node_id.has_value()
        && contains_node(dirty, *function.output_node_id);
    result.actor_subtree_affected = function.generates_actor
        || dirty_actor_instruction_exists(function, dirty);
    result.parent_propagation_required = result.function_outputs_affected;

    if (!result.dirty_instructions.empty()) {
        add_reason(result.reasons, InvalidationReason::instruction_dirty);
    }
    if (result.function_outputs_affected) {
        add_reason(result.reasons, InvalidationReason::function_outputs_affected);
    }
    if (result.actor_subtree_affected) {
        add_reason(result.reasons, InvalidationReason::actor_subtree_affected);
    }
    if (result.parent_propagation_required) {
        add_reason(result.reasons, InvalidationReason::parent_propagation_required);
    }

    return result;
}

const char* to_string(InvalidationReason reason) noexcept
{
    switch (reason) {
    case InvalidationReason::instruction_dirty:
        return "instruction_dirty";
    case InvalidationReason::function_outputs_affected:
        return "function_outputs_affected";
    case InvalidationReason::actor_subtree_affected:
        return "actor_subtree_affected";
    case InvalidationReason::parent_propagation_required:
        return "parent_propagation_required";
    }

    return "unknown";
}

} // namespace phoenix
