#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/partial_run.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

namespace phoenix {

enum class PartialRerunScopeStatus {
    resolved,
    invalid_request,
    no_actor_subtree_rerun,
    cached_subtree_available,
};

struct PartialRerunScopeRequest {
    const PartialRerunPlan* plan = nullptr;
    const FunctionDescriptor* function = nullptr;
    std::vector<PortValue> inputs;
    std::unordered_map<NodeId, std::vector<PortValue>> input_defaults;
    ExecutionContext context;
    std::optional<ActorId> actor_id;
    std::optional<NodeId> caller_node_id;
    const CacheStore* cache_store = nullptr;
    CacheWriter* cache_writer = nullptr;
    GeometryPublicationLedger* publication_ledger = nullptr;
    RunElementIdAllocator* element_ids = nullptr;
    std::uint64_t label_registry_fingerprint = 0;
    std::string kernel_version;
    std::string adapter_version;
    std::string repair_policy_version;
};

struct PartialRerunScopeResult {
    PartialRerunScopeStatus status = PartialRerunScopeStatus::invalid_request;
    std::optional<FunctionExecutionRequest> execution_request;
};

class PartialRerunScopeResolver {
public:
    [[nodiscard]] PartialRerunScopeResult resolve(const PartialRerunScopeRequest& request) const;
};

[[nodiscard]] const char* to_string(PartialRerunScopeStatus status) noexcept;

} // namespace phoenix
