#pragma once

#include "phoenix/execution.hpp"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace phoenix {

enum class PartialRerunScopeLookupStatus {
    found,
    not_found,
    ambiguous,
    invalid_request,
};

struct PartialRerunScopeRecord {
    FunctionId function_id;
    const FunctionDescriptor* function = nullptr;
    FunctionCallPath call_path;
    ActorId actor_id;
    bool generates_actor = false;
    std::optional<NodeId> caller_node_id;
    std::optional<std::size_t> parent_scope_index;
    std::vector<PortValue> inputs;
    std::unordered_map<NodeId, std::vector<PortValue>> input_defaults;
    SeedValue global_seed = 0;
};

struct PartialRerunScopeLookupResult {
    PartialRerunScopeLookupStatus status = PartialRerunScopeLookupStatus::not_found;
    std::optional<std::size_t> scope_index;
    const PartialRerunScopeRecord* scope = nullptr;
};

class PartialRerunScopeIndex final : public FunctionExecutionScopeTraceSink {
public:
    [[nodiscard]] std::size_t add_scope(PartialRerunScopeRecord scope);
    [[nodiscard]] std::size_t record_scope(FunctionExecutionScopeRecord scope) override;

    [[nodiscard]] const std::vector<PartialRerunScopeRecord>& scopes() const noexcept;
    [[nodiscard]] PartialRerunScopeLookupResult find_by_actor_id(const ActorId& actor_id) const;
    [[nodiscard]] PartialRerunScopeLookupResult find_by_call_path(
        const FunctionCallPath& call_path) const;
    [[nodiscard]] PartialRerunScopeLookupResult find_nearest_actor_scope(
        const FunctionCallPath& dirty_call_path) const;
    [[nodiscard]] PartialRerunScopeLookupResult find_parent_actor_scope(
        std::size_t scope_index) const;

private:
    std::vector<PartialRerunScopeRecord> scopes_;
};

[[nodiscard]] const char* to_string(PartialRerunScopeLookupStatus status) noexcept;

} // namespace phoenix
