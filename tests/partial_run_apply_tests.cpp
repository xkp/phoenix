#include "phoenix/cache.hpp"
#include "phoenix/partial_run_apply.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using phoenix::FunctionDescriptor;
using phoenix::InstructionDescriptor;
using phoenix::PortDescriptor;
using phoenix::PortDirection;

PortDescriptor make_output_port(const char* id, const char* type)
{
    return PortDescriptor{id, type, PortDirection::output};
}

phoenix::ActorNode make_actor(
    const char* id,
    std::vector<phoenix::ActorNode> children = {})
{
    phoenix::ActorNode actor;
    actor.id = id;
    actor.children = std::move(children);
    return actor;
}

phoenix::SceneRoot make_scene()
{
    phoenix::SceneRoot scene;
    scene.root = make_actor(
        "root",
        {
            make_actor("left"),
            make_actor("target", {make_actor("old-child")}),
            make_actor("right"),
        });
    return scene;
}

phoenix::PartialRerunPlan make_actor_subtree_plan(
    phoenix::CacheKey key,
    bool cache_hit)
{
    phoenix::PartialRerunPlan plan;
    plan.invalidation.actor_subtree_affected = true;
    plan.actor_subtree_key = std::move(key);
    plan.actor_subtree_cache_hit = cache_hit;
    return plan;
}

FunctionDescriptor make_rerun_function(const char* instruction_kind)
{
    FunctionDescriptor function;
    function.id = "rerun-target";
    function.generates_actor = true;

    InstructionDescriptor instruction;
    instruction.id = 1;
    instruction.kind = instruction_kind;
    instruction.generates_actor = true;
    instruction.has_else_port = false;
    instruction.output_ports = {make_output_port("output", "geometry")};
    function.instructions = {instruction};
    return function;
}

phoenix::FunctionExecutionRequest make_rerun_request(
    const FunctionDescriptor& function,
    const char* actor_id)
{
    phoenix::CallStack stack;
    stack.push(phoenix::CallFrame{
        function.id,
        {"root", "rerun-target"},
        std::nullopt,
        actor_id,
    });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root", "rerun-target"};
    request.call_stack = stack;
    return request;
}

bool test_applies_cached_actor_subtree()
{
    auto scene = make_scene();
    const phoenix::CacheKey key{"actor-subtree-key"};

    phoenix::MemoryCacheStore cache_store;
    phoenix::ActorSubtreeCacheEntry entry;
    entry.key = key;
    entry.actor = make_actor("target", {make_actor("new-child")});
    entry.actor.name = "updated";
    cache_store.put_actor_subtree(entry);

    const auto plan = make_actor_subtree_plan(key, true);

    phoenix::PartialRerunApplier applier{};
    const auto result = applier.apply(phoenix::PartialRerunApplyRequest{
        &scene,
        &plan,
        &cache_store,
    });

    return result.status == phoenix::PartialRerunApplyStatus::applied_cached_actor_subtree
        && result.scene_update_status.has_value()
        && *result.scene_update_status == phoenix::SceneUpdateStatus::applied
        && !result.replaced_root
        && scene.root.children.size() == 3
        && scene.root.children[0].id == "left"
        && scene.root.children[1].id == "target"
        && scene.root.children[1].name.has_value()
        && *scene.root.children[1].name == "updated"
        && scene.root.children[1].children.size() == 1
        && scene.root.children[1].children[0].id == "new-child"
        && scene.root.children[2].id == "right";
}

bool test_applies_cached_root_subtree()
{
    auto scene = make_scene();
    const phoenix::CacheKey key{"root-subtree-key"};

    phoenix::MemoryCacheStore cache_store;
    phoenix::ActorSubtreeCacheEntry entry;
    entry.key = key;
    entry.actor = make_actor("root", {make_actor("new-root-child")});
    cache_store.put_actor_subtree(entry);

    const auto plan = make_actor_subtree_plan(key, true);

    phoenix::PartialRerunApplier applier{};
    const auto result = applier.apply(phoenix::PartialRerunApplyRequest{
        &scene,
        &plan,
        &cache_store,
    });

    return result.status == phoenix::PartialRerunApplyStatus::applied_cached_actor_subtree
        && result.replaced_root
        && scene.root.id == "root"
        && scene.root.children.size() == 1
        && scene.root.children[0].id == "new-root-child";
}

bool test_cache_miss_requires_rerun()
{
    auto scene = make_scene();
    const auto plan = make_actor_subtree_plan(phoenix::CacheKey{"actor-subtree-key"}, false);

    phoenix::PartialRerunApplier applier{};
    const auto result = applier.apply(phoenix::PartialRerunApplyRequest{
        &scene,
        &plan,
        nullptr,
    });

    return result.status == phoenix::PartialRerunApplyStatus::rerun_required
        && scene.root.children[1].children[0].id == "old-child";
}

bool test_cache_miss_runs_executor_and_applies_actor_subtree()
{
    auto scene = make_scene();
    const auto plan = make_actor_subtree_plan(phoenix::CacheKey{"actor-subtree-key"}, false);
    const auto function = make_rerun_function("rerun_actor");
    const auto execution_request = make_rerun_request(function, "target");

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "rerun_actor",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("rerun")}},
                std::nullopt,
            };
        });

    const phoenix::FunctionExecutor executor(registry);
    phoenix::PartialRerunApplier applier{};
    const auto result = applier.apply(phoenix::PartialRerunApplyRequest{
        &scene,
        &plan,
        nullptr,
        &executor,
        &execution_request,
    });

    return result.status == phoenix::PartialRerunApplyStatus::applied_rerun_actor_subtree
        && result.execution_status.has_value()
        && *result.execution_status == phoenix::FunctionExecutionStatus::completed
        && result.scene_update_status.has_value()
        && *result.scene_update_status == phoenix::SceneUpdateStatus::applied
        && scene.root.children[1].id == "target"
        && scene.root.children[1].children.empty();
}

bool test_rerun_failure_leaves_scene_unchanged()
{
    auto scene = make_scene();
    const auto plan = make_actor_subtree_plan(phoenix::CacheKey{"actor-subtree-key"}, false);
    const auto function = make_rerun_function("missing_rerun_handler");
    const auto execution_request = make_rerun_request(function, "target");

    phoenix::InstructionRegistry registry;
    const phoenix::FunctionExecutor executor(registry);
    phoenix::PartialRerunApplier applier{};
    const auto result = applier.apply(phoenix::PartialRerunApplyRequest{
        &scene,
        &plan,
        nullptr,
        &executor,
        &execution_request,
    });

    return result.status == phoenix::PartialRerunApplyStatus::rerun_failed
        && result.execution_status.has_value()
        && *result.execution_status == phoenix::FunctionExecutionStatus::missing_handler
        && scene.root.children[1].id == "target"
        && scene.root.children[1].children.size() == 1
        && scene.root.children[1].children[0].id == "old-child";
}

bool test_missing_actor_subtree_key_is_invalid()
{
    auto scene = make_scene();
    phoenix::PartialRerunPlan plan;
    plan.invalidation.actor_subtree_affected = true;
    plan.actor_subtree_cache_hit = true;

    phoenix::MemoryCacheStore cache_store;
    phoenix::PartialRerunApplier applier{};
    const auto result = applier.apply(phoenix::PartialRerunApplyRequest{
        &scene,
        &plan,
        &cache_store,
    });

    return result.status == phoenix::PartialRerunApplyStatus::invalid_request
        && scene.root.children[1].children[0].id == "old-child";
}

bool test_planned_cache_hit_missing_at_apply_time()
{
    auto scene = make_scene();
    phoenix::MemoryCacheStore cache_store;
    const auto plan = make_actor_subtree_plan(phoenix::CacheKey{"actor-subtree-key"}, true);

    phoenix::PartialRerunApplier applier{};
    const auto result = applier.apply(phoenix::PartialRerunApplyRequest{
        &scene,
        &plan,
        &cache_store,
    });

    return result.status == phoenix::PartialRerunApplyStatus::cache_entry_missing
        && scene.root.children[1].children[0].id == "old-child";
}

bool test_cached_actor_missing_from_scene_reports_scene_update_failure()
{
    auto scene = make_scene();
    const phoenix::CacheKey key{"actor-subtree-key"};

    phoenix::MemoryCacheStore cache_store;
    phoenix::ActorSubtreeCacheEntry entry;
    entry.key = key;
    entry.actor = make_actor("missing");
    cache_store.put_actor_subtree(entry);

    const auto plan = make_actor_subtree_plan(key, true);

    phoenix::PartialRerunApplier applier{};
    const auto result = applier.apply(phoenix::PartialRerunApplyRequest{
        &scene,
        &plan,
        &cache_store,
    });

    return result.status == phoenix::PartialRerunApplyStatus::scene_update_failed
        && result.scene_update_status.has_value()
        && *result.scene_update_status == phoenix::SceneUpdateStatus::actor_not_found
        && scene.root.children[1].children[0].id == "old-child";
}

bool test_null_scene_or_plan_is_invalid()
{
    auto scene = make_scene();
    const auto plan = make_actor_subtree_plan(phoenix::CacheKey{"actor-subtree-key"}, false);
    phoenix::PartialRerunApplier applier{};

    const auto no_scene = applier.apply(phoenix::PartialRerunApplyRequest{
        nullptr,
        &plan,
        nullptr,
    });
    const auto no_plan = applier.apply(phoenix::PartialRerunApplyRequest{
        &scene,
        nullptr,
        nullptr,
    });

    return no_scene.status == phoenix::PartialRerunApplyStatus::invalid_request
        && no_plan.status == phoenix::PartialRerunApplyStatus::invalid_request;
}

bool test_status_strings_are_stable()
{
    return std::string{phoenix::to_string(
               phoenix::PartialRerunApplyStatus::applied_cached_actor_subtree)}
            == "applied_cached_actor_subtree"
        && std::string{phoenix::to_string(
               phoenix::PartialRerunApplyStatus::applied_rerun_actor_subtree)}
            == "applied_rerun_actor_subtree"
        && std::string{phoenix::to_string(phoenix::PartialRerunApplyStatus::rerun_required)}
            == "rerun_required"
        && std::string{phoenix::to_string(phoenix::PartialRerunApplyStatus::invalid_request)}
            == "invalid_request"
        && std::string{phoenix::to_string(phoenix::PartialRerunApplyStatus::cache_entry_missing)}
            == "cache_entry_missing"
        && std::string{phoenix::to_string(phoenix::PartialRerunApplyStatus::scene_update_failed)}
            == "scene_update_failed"
        && std::string{phoenix::to_string(phoenix::PartialRerunApplyStatus::rerun_failed)}
            == "rerun_failed"
        && std::string{phoenix::to_string(phoenix::PartialRerunApplyStatus::rerun_actor_missing)}
            == "rerun_actor_missing";
}

bool run_test(const char* name, bool (*test_fn)())
{
    const bool passed = test_fn();
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << '\n';
    return passed;
}

} // namespace

int main()
{
    bool ok = true;

    ok = run_test("applies cached actor subtree", test_applies_cached_actor_subtree) && ok;
    ok = run_test("applies cached root subtree", test_applies_cached_root_subtree) && ok;
    ok = run_test("cache miss requires rerun", test_cache_miss_requires_rerun) && ok;
    ok = run_test("cache miss runs executor and applies actor subtree", test_cache_miss_runs_executor_and_applies_actor_subtree) && ok;
    ok = run_test("rerun failure leaves scene unchanged", test_rerun_failure_leaves_scene_unchanged) && ok;
    ok = run_test("missing actor subtree key is invalid", test_missing_actor_subtree_key_is_invalid) && ok;
    ok = run_test("planned cache hit missing at apply time", test_planned_cache_hit_missing_at_apply_time) && ok;
    ok = run_test("cached actor missing from scene reports scene update failure", test_cached_actor_missing_from_scene_reports_scene_update_failure) && ok;
    ok = run_test("null scene or plan is invalid", test_null_scene_or_plan_is_invalid) && ok;
    ok = run_test("status strings are stable", test_status_strings_are_stable) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
