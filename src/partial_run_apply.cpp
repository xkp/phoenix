#include "phoenix/partial_run_apply.hpp"

namespace phoenix {
namespace {

PartialRerunApplyResult apply_actor_to_scene(
    const SceneUpdater& scene_updater,
    SceneRoot& scene,
    ActorNode actor,
    PartialRerunApplyStatus applied_status)
{
    const auto scene_update = scene_updater.replace_actor_subtree(scene, std::move(actor));
    if (scene_update.status != SceneUpdateStatus::applied) {
        return PartialRerunApplyResult{
            PartialRerunApplyStatus::scene_update_failed,
            scene_update.status,
            std::nullopt,
            scene_update.replaced_root,
        };
    }

    return PartialRerunApplyResult{
        applied_status,
        scene_update.status,
        std::nullopt,
        scene_update.replaced_root,
    };
}

PartialRerunApplyResult rerun_actor_subtree(
    const SceneUpdater& scene_updater,
    const PartialRerunApplyRequest& request)
{
    if (request.executor == nullptr || request.execution_request == nullptr) {
        return PartialRerunApplyResult{PartialRerunApplyStatus::rerun_required};
    }

    const auto execution = request.executor->run(*request.execution_request);
    if (execution.status != FunctionExecutionStatus::completed) {
        return PartialRerunApplyResult{
            PartialRerunApplyStatus::rerun_failed,
            std::nullopt,
            execution.status,
            false,
        };
    }

    if (!execution.actor.has_value()) {
        return PartialRerunApplyResult{
            PartialRerunApplyStatus::rerun_actor_missing,
            std::nullopt,
            execution.status,
            false,
        };
    }

    auto result = apply_actor_to_scene(
        scene_updater,
        *request.scene,
        *execution.actor,
        PartialRerunApplyStatus::applied_rerun_actor_subtree);
    result.execution_status = execution.status;
    return result;
}

} // namespace

PartialRerunApplyResult PartialRerunApplier::apply(
    const PartialRerunApplyRequest& request) const
{
    if (request.scene == nullptr || request.plan == nullptr) {
        return PartialRerunApplyResult{PartialRerunApplyStatus::invalid_request};
    }

    const auto& plan = *request.plan;
    if (!plan.invalidation.actor_subtree_affected) {
        return PartialRerunApplyResult{PartialRerunApplyStatus::rerun_required};
    }

    if (!plan.actor_subtree_key.has_value()) {
        return PartialRerunApplyResult{PartialRerunApplyStatus::invalid_request};
    }

    if (!plan.actor_subtree_cache_hit) {
        return rerun_actor_subtree(scene_updater_, request);
    }

    if (request.cache_store == nullptr) {
        return PartialRerunApplyResult{PartialRerunApplyStatus::invalid_request};
    }

    const auto cached_subtree = request.cache_store->find_actor_subtree(*plan.actor_subtree_key);
    if (!cached_subtree.has_value()) {
        return PartialRerunApplyResult{PartialRerunApplyStatus::cache_entry_missing};
    }

    return apply_actor_to_scene(
        scene_updater_,
        *request.scene,
        cached_subtree->actor,
        PartialRerunApplyStatus::applied_cached_actor_subtree
    );
}

const char* to_string(PartialRerunApplyStatus status) noexcept
{
    switch (status) {
    case PartialRerunApplyStatus::applied_cached_actor_subtree:
        return "applied_cached_actor_subtree";
    case PartialRerunApplyStatus::applied_rerun_actor_subtree:
        return "applied_rerun_actor_subtree";
    case PartialRerunApplyStatus::rerun_required:
        return "rerun_required";
    case PartialRerunApplyStatus::invalid_request:
        return "invalid_request";
    case PartialRerunApplyStatus::cache_entry_missing:
        return "cache_entry_missing";
    case PartialRerunApplyStatus::scene_update_failed:
        return "scene_update_failed";
    case PartialRerunApplyStatus::rerun_failed:
        return "rerun_failed";
    case PartialRerunApplyStatus::rerun_actor_missing:
        return "rerun_actor_missing";
    }

    return "unknown";
}

} // namespace phoenix
