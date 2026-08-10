#include "phoenix/partial_run_scope_discovery.hpp"

namespace phoenix {
namespace {

PartialRerunScopeRequest make_scope_request(
    const PartialRerunPlan& plan,
    const PartialRerunScopeRecord& scope)
{
    PartialRerunScopeRequest request;
    request.plan = &plan;
    request.function = scope.function;
    request.inputs = scope.inputs;
    request.input_defaults = scope.input_defaults;
    request.context.function_id = scope.function_id;
    request.context.call_path = scope.call_path;
    request.context.global_seed = scope.global_seed;
    request.actor_id = scope.actor_id;
    request.caller_node_id = scope.caller_node_id;
    return request;
}

} // namespace

PartialRerunScopeDiscoveryResult PartialRerunScopeDiscovery::discover(
    const PartialRerunScopeDiscoveryRequest& request) const
{
    if (request.plan == nullptr
        || request.scope_index == nullptr
        || request.dirty_call_path.empty()) {
        return PartialRerunScopeDiscoveryResult{PartialRerunScopeDiscoveryStatus::invalid_request};
    }

    if (!request.plan->invalidation.actor_subtree_affected) {
        return PartialRerunScopeDiscoveryResult{
            PartialRerunScopeDiscoveryStatus::no_actor_subtree_rerun,
        };
    }

    if (request.plan->actor_subtree_cache_hit) {
        return PartialRerunScopeDiscoveryResult{
            PartialRerunScopeDiscoveryStatus::cached_subtree_available,
        };
    }

    auto lookup = request.scope_index->find_nearest_actor_scope(request.dirty_call_path);
    if (lookup.status == PartialRerunScopeLookupStatus::not_found) {
        return PartialRerunScopeDiscoveryResult{
            PartialRerunScopeDiscoveryStatus::scope_not_found,
            lookup.status,
        };
    }

    if (lookup.status == PartialRerunScopeLookupStatus::ambiguous) {
        return PartialRerunScopeDiscoveryResult{
            PartialRerunScopeDiscoveryStatus::scope_ambiguous,
            lookup.status,
        };
    }

    if (request.propagate_to_parent_actor_scope && lookup.scope_index.has_value()) {
        const auto parent_lookup =
            request.scope_index->find_parent_actor_scope(*lookup.scope_index);
        if (parent_lookup.status == PartialRerunScopeLookupStatus::found) {
            lookup = parent_lookup;
        } else if (parent_lookup.status == PartialRerunScopeLookupStatus::invalid_request) {
            return PartialRerunScopeDiscoveryResult{
                PartialRerunScopeDiscoveryStatus::invalid_request,
                parent_lookup.status,
            };
        }
    }

    if (lookup.status == PartialRerunScopeLookupStatus::invalid_request
        || lookup.scope == nullptr
        || lookup.scope->function == nullptr) {
        return PartialRerunScopeDiscoveryResult{
            PartialRerunScopeDiscoveryStatus::invalid_request,
            lookup.status,
        };
    }

    return PartialRerunScopeDiscoveryResult{
        PartialRerunScopeDiscoveryStatus::discovered,
        lookup.status,
        make_scope_request(*request.plan, *lookup.scope),
    };
}

const char* to_string(PartialRerunScopeDiscoveryStatus status) noexcept
{
    switch (status) {
    case PartialRerunScopeDiscoveryStatus::discovered:
        return "discovered";
    case PartialRerunScopeDiscoveryStatus::invalid_request:
        return "invalid_request";
    case PartialRerunScopeDiscoveryStatus::no_actor_subtree_rerun:
        return "no_actor_subtree_rerun";
    case PartialRerunScopeDiscoveryStatus::cached_subtree_available:
        return "cached_subtree_available";
    case PartialRerunScopeDiscoveryStatus::scope_not_found:
        return "scope_not_found";
    case PartialRerunScopeDiscoveryStatus::scope_ambiguous:
        return "scope_ambiguous";
    }

    return "unknown";
}

} // namespace phoenix
