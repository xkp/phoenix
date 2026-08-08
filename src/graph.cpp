#include "phoenix/graph.hpp"

namespace phoenix {

GraphIndex::GraphIndex(const FunctionDescriptor& function)
    : function_(&function)
{
    for (const auto& instruction : function.instructions) {
        by_id_.emplace(instruction.id, &instruction);
        incoming_edges_.emplace(instruction.id, std::vector<const EdgeDescriptor*>{});
        outgoing_edges_.emplace(instruction.id, std::vector<const EdgeDescriptor*>{});
    }

    for (const auto& edge : function.edges) {
        auto outgoing_it = outgoing_edges_.find(edge.from_node);
        if (outgoing_it != outgoing_edges_.end()) {
            outgoing_it->second.push_back(&edge);
        }

        auto incoming_it = incoming_edges_.find(edge.to_node);
        if (incoming_it != incoming_edges_.end()) {
            incoming_it->second.push_back(&edge);
        }
    }
}

const FunctionDescriptor& GraphIndex::function() const noexcept
{
    return *function_;
}

const InstructionDescriptor* GraphIndex::find_instruction(NodeId id) const noexcept
{
    const auto it = by_id_.find(id);
    if (it == by_id_.end()) {
        return nullptr;
    }

    return it->second;
}

const std::vector<const EdgeDescriptor*>& GraphIndex::incoming_edges(NodeId id) const noexcept
{
    static const std::vector<const EdgeDescriptor*> empty;

    const auto it = incoming_edges_.find(id);
    if (it == incoming_edges_.end()) {
        return empty;
    }

    return it->second;
}

const std::vector<const EdgeDescriptor*>& GraphIndex::outgoing_edges(NodeId id) const noexcept
{
    static const std::vector<const EdgeDescriptor*> empty;

    const auto it = outgoing_edges_.find(id);
    if (it == outgoing_edges_.end()) {
        return empty;
    }

    return it->second;
}

} // namespace phoenix
