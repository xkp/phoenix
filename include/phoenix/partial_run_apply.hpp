#pragma once

#include "phoenix/cache.hpp"
#include "phoenix/execution.hpp"
#include "phoenix/partial_run.hpp"
#include "phoenix/scene_update.hpp"

#include <optional>

namespace phoenix {

enum class PartialRerunApplyStatus {
    applied_cached_actor_subtree,
    applied_rerun_actor_subtree,
    rerun_required,
    invalid_request,
    cache_entry_missing,
    scene_update_failed,
    rerun_failed,
    rerun_actor_missing,
};

struct PartialRerunApplyRequest {
    SceneRoot* scene = nullptr;
    const PartialRerunPlan* plan = nullptr;
    const CacheStore* cache_store = nullptr;
    const FunctionExecutor* executor = nullptr;
    const FunctionExecutionRequest* execution_request = nullptr;
};

struct PartialRerunApplyResult {
    PartialRerunApplyStatus status = PartialRerunApplyStatus::invalid_request;
    std::optional<SceneUpdateStatus> scene_update_status;
    std::optional<FunctionExecutionStatus> execution_status;
    bool replaced_root = false;
};

class PartialRerunApplier {
public:
    [[nodiscard]] PartialRerunApplyResult apply(const PartialRerunApplyRequest& request) const;

private:
    SceneUpdater scene_updater_;
};

[[nodiscard]] const char* to_string(PartialRerunApplyStatus status) noexcept;

} // namespace phoenix
