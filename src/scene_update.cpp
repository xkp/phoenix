#include "phoenix/scene_update.hpp"

#include <utility>

namespace phoenix {
namespace {

bool replace_child_subtree(ActorNode& parent, const ActorNode& replacement)
{
    for (auto& child : parent.children) {
        if (child.id == replacement.id) {
            child = replacement;
            return true;
        }

        if (replace_child_subtree(child, replacement)) {
            return true;
        }
    }

    return false;
}

} // namespace

SceneUpdateResult SceneUpdater::replace_actor_subtree(
    SceneRoot& scene,
    ActorNode replacement) const
{
    if (replacement.id.empty()) {
        return SceneUpdateResult{SceneUpdateStatus::invalid_request, false};
    }

    if (scene.root.id == replacement.id) {
        scene.root = std::move(replacement);
        return SceneUpdateResult{SceneUpdateStatus::applied, true};
    }

    if (replace_child_subtree(scene.root, replacement)) {
        return SceneUpdateResult{SceneUpdateStatus::applied, false};
    }

    return SceneUpdateResult{SceneUpdateStatus::actor_not_found, false};
}

const char* to_string(SceneUpdateStatus status) noexcept
{
    switch (status) {
    case SceneUpdateStatus::applied:
        return "applied";
    case SceneUpdateStatus::actor_not_found:
        return "actor_not_found";
    case SceneUpdateStatus::invalid_request:
        return "invalid_request";
    }

    return "unknown";
}

} // namespace phoenix
