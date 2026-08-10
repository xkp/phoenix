#include "phoenix/partial_run_scope_index.hpp"

#include <utility>

namespace phoenix {
namespace {

bool is_prefix_path(const FunctionCallPath& prefix, const FunctionCallPath& path)
{
    if (prefix.size() > path.size()) {
        return false;
    }

    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (prefix[i] != path[i]) {
            return false;
        }
    }

    return true;
}

bool is_actor_boundary(const PartialRerunScopeRecord& scope)
{
    return scope.generates_actor || !scope.parent_scope_index.has_value();
}

PartialRerunScopeLookupResult found_result(
    std::size_t index,
    const PartialRerunScopeRecord& scope)
{
    return PartialRerunScopeLookupResult{
        PartialRerunScopeLookupStatus::found,
        index,
        &scope,
    };
}

} // namespace

std::size_t PartialRerunScopeIndex::add_scope(PartialRerunScopeRecord scope)
{
    scopes_.push_back(std::move(scope));
    return scopes_.size() - 1;
}

std::size_t PartialRerunScopeIndex::record_scope(FunctionExecutionScopeRecord scope)
{
    PartialRerunScopeRecord record;
    record.function_id = std::move(scope.function_id);
    record.function = scope.function;
    record.call_path = std::move(scope.call_path);
    record.actor_id = std::move(scope.actor_id);
    record.generates_actor = scope.generates_actor;
    record.caller_node_id = scope.caller_node_id;
    record.parent_scope_index = scope.parent_scope_index;
    record.inputs = std::move(scope.inputs);
    record.input_defaults = std::move(scope.input_defaults);
    record.global_seed = scope.global_seed;
    return add_scope(std::move(record));
}

const std::vector<PartialRerunScopeRecord>& PartialRerunScopeIndex::scopes() const noexcept
{
    return scopes_;
}

PartialRerunScopeLookupResult PartialRerunScopeIndex::find_by_actor_id(
    const ActorId& actor_id) const
{
    if (actor_id.empty()) {
        return PartialRerunScopeLookupResult{PartialRerunScopeLookupStatus::invalid_request};
    }

    std::optional<std::size_t> matched_index;
    for (std::size_t i = 0; i < scopes_.size(); ++i) {
        if (scopes_[i].actor_id != actor_id) {
            continue;
        }

        if (matched_index.has_value()) {
            return PartialRerunScopeLookupResult{PartialRerunScopeLookupStatus::ambiguous};
        }

        matched_index = i;
    }

    if (!matched_index.has_value()) {
        return PartialRerunScopeLookupResult{PartialRerunScopeLookupStatus::not_found};
    }

    return found_result(*matched_index, scopes_[*matched_index]);
}

PartialRerunScopeLookupResult PartialRerunScopeIndex::find_by_call_path(
    const FunctionCallPath& call_path) const
{
    if (call_path.empty()) {
        return PartialRerunScopeLookupResult{PartialRerunScopeLookupStatus::invalid_request};
    }

    std::optional<std::size_t> matched_index;
    for (std::size_t i = 0; i < scopes_.size(); ++i) {
        if (scopes_[i].call_path != call_path) {
            continue;
        }

        if (matched_index.has_value()) {
            return PartialRerunScopeLookupResult{PartialRerunScopeLookupStatus::ambiguous};
        }

        matched_index = i;
    }

    if (!matched_index.has_value()) {
        return PartialRerunScopeLookupResult{PartialRerunScopeLookupStatus::not_found};
    }

    return found_result(*matched_index, scopes_[*matched_index]);
}

PartialRerunScopeLookupResult PartialRerunScopeIndex::find_nearest_actor_scope(
    const FunctionCallPath& dirty_call_path) const
{
    if (dirty_call_path.empty()) {
        return PartialRerunScopeLookupResult{PartialRerunScopeLookupStatus::invalid_request};
    }

    std::optional<std::size_t> matched_index;
    for (std::size_t i = 0; i < scopes_.size(); ++i) {
        const auto& scope = scopes_[i];
        if (!is_actor_boundary(scope) || !is_prefix_path(scope.call_path, dirty_call_path)) {
            continue;
        }

        if (!matched_index.has_value()
            || scope.call_path.size() > scopes_[*matched_index].call_path.size()) {
            matched_index = i;
        }
    }

    if (!matched_index.has_value()) {
        return PartialRerunScopeLookupResult{PartialRerunScopeLookupStatus::not_found};
    }

    return found_result(*matched_index, scopes_[*matched_index]);
}

PartialRerunScopeLookupResult PartialRerunScopeIndex::find_parent_actor_scope(
    std::size_t scope_index) const
{
    if (scope_index >= scopes_.size()) {
        return PartialRerunScopeLookupResult{PartialRerunScopeLookupStatus::invalid_request};
    }

    auto parent_index = scopes_[scope_index].parent_scope_index;
    while (parent_index.has_value()) {
        if (*parent_index >= scopes_.size()) {
            return PartialRerunScopeLookupResult{PartialRerunScopeLookupStatus::invalid_request};
        }

        const auto& parent = scopes_[*parent_index];
        if (is_actor_boundary(parent)) {
            return found_result(*parent_index, parent);
        }

        parent_index = parent.parent_scope_index;
    }

    return PartialRerunScopeLookupResult{PartialRerunScopeLookupStatus::not_found};
}

const char* to_string(PartialRerunScopeLookupStatus status) noexcept
{
    switch (status) {
    case PartialRerunScopeLookupStatus::found:
        return "found";
    case PartialRerunScopeLookupStatus::not_found:
        return "not_found";
    case PartialRerunScopeLookupStatus::ambiguous:
        return "ambiguous";
    case PartialRerunScopeLookupStatus::invalid_request:
        return "invalid_request";
    }

    return "unknown";
}

} // namespace phoenix
