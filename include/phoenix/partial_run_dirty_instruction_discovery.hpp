#pragma once

#include "phoenix/partial_run_instruction_index.hpp"
#include "phoenix/partial_run_scope_discovery.hpp"

#include <vector>

namespace phoenix {

enum class PartialRerunDirtyInstructionDiscoveryStatus {
    discovered,
    partially_discovered,
    invalid_request,
    no_actor_subtree_rerun,
    cached_subtree_available,
    no_instruction_traces,
    scope_not_found,
    scope_ambiguous,
};

struct PartialRerunDirtyInstructionDiscoveryRequest {
    const PartialRerunPlan* plan = nullptr;
    const PartialRerunInstructionIndex* instruction_index = nullptr;
    const PartialRerunScopeIndex* scope_index = nullptr;
    FunctionId function_id;
    NodeId node_id = 0;
};

struct PartialRerunDirtyInstructionDiscoveryResult {
    PartialRerunDirtyInstructionDiscoveryStatus status =
        PartialRerunDirtyInstructionDiscoveryStatus::invalid_request;
    std::vector<PartialRerunScopeDiscoveryResult> discoveries;
    std::vector<PartialRerunScopeRequest> scope_requests;
};

class PartialRerunDirtyInstructionDiscovery {
public:
    [[nodiscard]] PartialRerunDirtyInstructionDiscoveryResult discover(
        const PartialRerunDirtyInstructionDiscoveryRequest& request) const;
};

[[nodiscard]] const char* to_string(
    PartialRerunDirtyInstructionDiscoveryStatus status) noexcept;

} // namespace phoenix
