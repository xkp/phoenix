#pragma once

#include "phoenix/actors.hpp"

namespace phoenix {

enum class SceneUpdateStatus {
    applied,
    actor_not_found,
    invalid_request,
};

struct SceneUpdateResult {
    SceneUpdateStatus status = SceneUpdateStatus::invalid_request;
    bool replaced_root = false;
};

class SceneUpdater {
public:
    [[nodiscard]] SceneUpdateResult replace_actor_subtree(
        SceneRoot& scene,
        ActorNode replacement) const;
};

[[nodiscard]] const char* to_string(SceneUpdateStatus status) noexcept;

} // namespace phoenix
