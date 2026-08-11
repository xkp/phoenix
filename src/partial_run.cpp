#include "phoenix/partial_run.hpp"

namespace phoenix {

namespace {

std::optional<SeedValue> effective_seed_for(
    NodeId node_id,
    const std::unordered_map<NodeId, SeedValue>& seeds)
{
    const auto it = seeds.find(node_id);
    if (it == seeds.end()) {
        return std::nullopt;
    }

    return it->second;
}

bool has_instruction_cache_hit(const CacheStore* cache_store, const CacheKey& key)
{
    return cache_store != nullptr && cache_store->find_instruction(key).has_value();
}

bool has_function_call_cache_hit(const CacheStore* cache_store, const CacheKey& key)
{
    return cache_store != nullptr && cache_store->find_function_call(key).has_value();
}

bool has_actor_subtree_cache_hit(const CacheStore* cache_store, const CacheKey& key)
{
    return cache_store != nullptr && cache_store->find_actor_subtree(key).has_value();
}

bool has_supplied_cache_identity(const CacheIdentity& identity)
{
    return !identity.function_id.empty()
        || !identity.call_path.empty()
        || !identity.graph_revision.empty()
        || !identity.input_fingerprint.empty()
        || identity.global_seed != 0;
}

} // namespace

PartialRerunPlan PartialRerunPlanner::plan(const PartialRerunRequest& request) const
{
    PartialRerunPlan plan;
    plan.invalidation = invalidation_planner_.plan(InvalidationRequest{
        request.function,
        request.changed_instructions,
    });

    const auto cache_identity = has_supplied_cache_identity(request.cache_identity)
        ? request.cache_identity
        : cache_identity_builder_.identity(CacheIdentityInput{
            request.function,
            request.call_path,
            request.inputs,
            request.global_seed,
        });

    for (const auto node_id : plan.invalidation.dirty_instructions) {
        InstructionCacheKeyInput key_input;
        key_input.identity = cache_identity;
        key_input.node_id = node_id;
        key_input.effective_seed = effective_seed_for(node_id, request.effective_instruction_seeds);

        InstructionRerunPlan instruction_plan;
        instruction_plan.node_id = node_id;
        instruction_plan.cache_key = cache_key_builder_.instruction_outputs(key_input);
        instruction_plan.cache_hit = has_instruction_cache_hit(
            request.cache_store,
            instruction_plan.cache_key);
        plan.instructions.push_back(std::move(instruction_plan));
    }

    if (plan.invalidation.function_outputs_affected) {
        FunctionCallCacheKeyInput key_input;
        key_input.identity = cache_identity;
        plan.function_call_key = cache_key_builder_.function_call(key_input);
        plan.function_call_cache_hit = has_function_call_cache_hit(
            request.cache_store,
            *plan.function_call_key);
    }

    if (plan.invalidation.actor_subtree_affected && request.actor_id.has_value()) {
        ActorSubtreeCacheKeyInput key_input;
        key_input.identity = cache_identity;
        key_input.actor_id = *request.actor_id;
        plan.actor_subtree_key = cache_key_builder_.actor_subtree(key_input);
        plan.actor_subtree_cache_hit = has_actor_subtree_cache_hit(
            request.cache_store,
            *plan.actor_subtree_key);
    }

    return plan;
}

} // namespace phoenix
