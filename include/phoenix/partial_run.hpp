#pragma once

#include "phoenix/cache.hpp"
#include "phoenix/common.hpp"
#include "phoenix/graph.hpp"
#include "phoenix/invalidation.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

namespace phoenix {

struct InstructionRerunPlan {
    NodeId node_id = 0;
    CacheKey cache_key;
    bool cache_hit = false;
};

struct PartialRerunRequest {
    const FunctionDescriptor* function = nullptr;
    std::vector<NodeId> changed_instructions;
    CacheIdentity cache_identity;
    std::unordered_map<NodeId, SeedValue> effective_instruction_seeds;
    std::optional<ActorId> actor_id;
    const CacheStore* cache_store = nullptr;
};

struct PartialRerunPlan {
    InvalidationResult invalidation;
    std::vector<InstructionRerunPlan> instructions;
    std::optional<CacheKey> function_call_key;
    bool function_call_cache_hit = false;
    std::optional<CacheKey> actor_subtree_key;
    bool actor_subtree_cache_hit = false;
};

class PartialRerunPlanner {
public:
    [[nodiscard]] PartialRerunPlan plan(const PartialRerunRequest& request) const;

private:
    InvalidationPlanner invalidation_planner_;
    CacheKeyBuilder cache_key_builder_;
};

} // namespace phoenix
