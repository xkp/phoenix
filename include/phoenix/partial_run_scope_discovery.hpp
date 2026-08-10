#pragma once

#include "phoenix/partial_run_scope.hpp"
#include "phoenix/partial_run_scope_index.hpp"

#include <optional>

namespace phoenix {

enum class PartialRerunScopeDiscoveryStatus {
    discovered,
    invalid_request,
    no_actor_subtree_rerun,
    cached_subtree_available,
    scope_not_found,
    scope_ambiguous,
};

struct PartialRerunScopeDiscoveryRequest {
    const PartialRerunPlan* plan = nullptr;
    const PartialRerunScopeIndex* scope_index = nullptr;
    FunctionCallPath dirty_call_path;
    bool propagate_to_parent_actor_scope = false;
};

struct PartialRerunScopeDiscoveryResult {
    PartialRerunScopeDiscoveryStatus status = PartialRerunScopeDiscoveryStatus::invalid_request;
    std::optional<PartialRerunScopeLookupStatus> lookup_status;
    std::optional<PartialRerunScopeRequest> scope_request;
};

class PartialRerunScopeDiscovery {
public:
    [[nodiscard]] PartialRerunScopeDiscoveryResult discover(
        const PartialRerunScopeDiscoveryRequest& request) const;
};

[[nodiscard]] const char* to_string(PartialRerunScopeDiscoveryStatus status) noexcept;

} // namespace phoenix
