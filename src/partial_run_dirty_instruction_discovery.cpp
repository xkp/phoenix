#include "phoenix/partial_run_dirty_instruction_discovery.hpp"

#include <sstream>
#include <unordered_set>
#include <utility>

namespace phoenix {
namespace {

void append_field(std::ostringstream& stream, const std::string& value)
{
    stream << value.size() << ':' << value;
}

std::string call_path_key(const FunctionCallPath& call_path)
{
    std::ostringstream stream;
    stream << call_path.size();
    for (const auto& segment : call_path) {
        stream << '|';
        append_field(stream, segment);
    }
    return stream.str();
}

PartialRerunDirtyInstructionDiscoveryStatus aggregate_status(
    const std::vector<PartialRerunScopeRequest>& scope_requests,
    const std::vector<PartialRerunScopeDiscoveryResult>& discoveries)
{
    if (scope_requests.empty()) {
        for (const auto& discovery : discoveries) {
            if (discovery.status == PartialRerunScopeDiscoveryStatus::scope_ambiguous) {
                return PartialRerunDirtyInstructionDiscoveryStatus::scope_ambiguous;
            }
        }

        for (const auto& discovery : discoveries) {
            if (discovery.status == PartialRerunScopeDiscoveryStatus::scope_not_found) {
                return PartialRerunDirtyInstructionDiscoveryStatus::scope_not_found;
            }
        }

        return PartialRerunDirtyInstructionDiscoveryStatus::invalid_request;
    }

    for (const auto& discovery : discoveries) {
        if (discovery.status != PartialRerunScopeDiscoveryStatus::discovered) {
            return PartialRerunDirtyInstructionDiscoveryStatus::partially_discovered;
        }
    }

    return PartialRerunDirtyInstructionDiscoveryStatus::discovered;
}

} // namespace

PartialRerunDirtyInstructionDiscoveryResult PartialRerunDirtyInstructionDiscovery::discover(
    const PartialRerunDirtyInstructionDiscoveryRequest& request) const
{
    if (request.plan == nullptr
        || request.instruction_index == nullptr
        || request.scope_index == nullptr
        || request.function_id.empty()
        || request.node_id == 0) {
        return PartialRerunDirtyInstructionDiscoveryResult{
            PartialRerunDirtyInstructionDiscoveryStatus::invalid_request,
        };
    }

    if (!request.plan->invalidation.actor_subtree_affected) {
        return PartialRerunDirtyInstructionDiscoveryResult{
            PartialRerunDirtyInstructionDiscoveryStatus::no_actor_subtree_rerun,
        };
    }

    if (request.plan->actor_subtree_cache_hit) {
        return PartialRerunDirtyInstructionDiscoveryResult{
            PartialRerunDirtyInstructionDiscoveryStatus::cached_subtree_available,
        };
    }

    const auto instruction_hits =
        request.instruction_index->find_by_function_and_node(
            request.function_id,
            request.node_id);
    if (instruction_hits.empty()) {
        return PartialRerunDirtyInstructionDiscoveryResult{
            PartialRerunDirtyInstructionDiscoveryStatus::no_instruction_traces,
        };
    }

    PartialRerunDirtyInstructionDiscoveryResult result;
    std::unordered_set<std::string> seen_call_paths;
    const PartialRerunScopeDiscovery scope_discovery;

    for (const auto instruction_index : instruction_hits) {
        if (instruction_index >= request.instruction_index->instructions().size()) {
            continue;
        }

        const auto& instruction =
            request.instruction_index->instructions()[instruction_index];
        const auto inserted =
            seen_call_paths.insert(call_path_key(instruction.call_path)).second;
        if (!inserted) {
            continue;
        }

        auto discovery = scope_discovery.discover(PartialRerunScopeDiscoveryRequest{
            request.plan,
            request.scope_index,
            instruction.call_path,
            request.plan->invalidation.parent_propagation_required,
        });
        if (discovery.scope_request.has_value()) {
            result.scope_requests.push_back(*discovery.scope_request);
        }
        result.discoveries.push_back(std::move(discovery));
    }

    result.status = aggregate_status(result.scope_requests, result.discoveries);
    return result;
}

const char* to_string(PartialRerunDirtyInstructionDiscoveryStatus status) noexcept
{
    switch (status) {
    case PartialRerunDirtyInstructionDiscoveryStatus::discovered:
        return "discovered";
    case PartialRerunDirtyInstructionDiscoveryStatus::partially_discovered:
        return "partially_discovered";
    case PartialRerunDirtyInstructionDiscoveryStatus::invalid_request:
        return "invalid_request";
    case PartialRerunDirtyInstructionDiscoveryStatus::no_actor_subtree_rerun:
        return "no_actor_subtree_rerun";
    case PartialRerunDirtyInstructionDiscoveryStatus::cached_subtree_available:
        return "cached_subtree_available";
    case PartialRerunDirtyInstructionDiscoveryStatus::no_instruction_traces:
        return "no_instruction_traces";
    case PartialRerunDirtyInstructionDiscoveryStatus::scope_not_found:
        return "scope_not_found";
    case PartialRerunDirtyInstructionDiscoveryStatus::scope_ambiguous:
        return "scope_ambiguous";
    }

    return "unknown";
}

} // namespace phoenix
