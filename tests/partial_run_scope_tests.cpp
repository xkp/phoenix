#include "phoenix/partial_run_apply.hpp"
#include "phoenix/partial_run_scope.hpp"

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

PortDescriptor make_input_port(const char* id, const char* type)
{
    return PortDescriptor{id, type, PortDirection::input};
}

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
            make_actor("before"),
            make_actor("target", {make_actor("old-child")}),
            make_actor("after"),
        });
    return scene;
}

FunctionDescriptor make_actor_function(const char* instruction_kind)
{
    FunctionDescriptor function;
    function.id = "actor-scope";
    function.input_ports = {make_input_port("input", "geometry")};
    function.generates_actor = true;

    InstructionDescriptor instruction;
    instruction.id = 7;
    instruction.kind = instruction_kind;
    instruction.generates_actor = true;
    instruction.has_else_port = false;
    instruction.input_ports = {make_input_port("input", "geometry")};
    instruction.output_ports = {make_output_port("output", "geometry")};
    function.instructions = {instruction};
    return function;
}

phoenix::PartialRerunPlan make_plan(bool actor_subtree_affected, bool cache_hit)
{
    phoenix::PartialRerunPlan plan;
    plan.invalidation.actor_subtree_affected = actor_subtree_affected;
    plan.actor_subtree_key = phoenix::CacheKey{"actor-subtree-key"};
    plan.actor_subtree_cache_hit = cache_hit;
    return plan;
}

phoenix::PartialRerunScopeRequest make_scope_request(
    const phoenix::PartialRerunPlan& plan,
    const FunctionDescriptor& function)
{
    phoenix::PartialRerunScopeRequest request;
    request.plan = &plan;
    request.function = &function;
    request.inputs = {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry("rerun-input")}};
    request.input_defaults[7] = {
        phoenix::PortValue{"count", phoenix::RuntimeValue::defaulted("int")},
    };
    request.context.call_path = {"root", "2:actor-scope"};
    request.context.global_seed = 99;
    request.actor_id = "target";
    request.caller_node_id = 2;
    return request;
}

bool test_resolves_execution_request_for_actor_subtree_cache_miss()
{
    const auto function = make_actor_function("rerun_actor");
    const auto plan = make_plan(true, false);
    const auto request = make_scope_request(plan, function);

    const phoenix::PartialRerunScopeResolver resolver;
    const auto result = resolver.resolve(request);

    const auto* frame = result.execution_request.has_value()
        ? result.execution_request->call_stack.current()
        : nullptr;

    return result.status == phoenix::PartialRerunScopeStatus::resolved
        && result.execution_request.has_value()
        && result.execution_request->function == &function
        && result.execution_request->inputs.size() == 1
        && result.execution_request->context.function_id == function.id
        && result.execution_request->context.call_path == request.context.call_path
        && result.execution_request->context.global_seed == 99
        && frame != nullptr
        && frame->function_id == function.id
        && frame->call_path == request.context.call_path
        && frame->caller_node_id.has_value()
        && *frame->caller_node_id == 2
        && frame->actor_id.has_value()
        && *frame->actor_id == "target";
}

bool test_cached_actor_subtree_skips_execution_resolution()
{
    const auto function = make_actor_function("rerun_actor");
    const auto plan = make_plan(true, true);
    const auto request = make_scope_request(plan, function);

    const phoenix::PartialRerunScopeResolver resolver;
    const auto result = resolver.resolve(request);

    return result.status == phoenix::PartialRerunScopeStatus::cached_subtree_available
        && !result.execution_request.has_value();
}

bool test_non_actor_subtree_plan_has_no_actor_rerun()
{
    const auto function = make_actor_function("rerun_actor");
    const auto plan = make_plan(false, false);
    const auto request = make_scope_request(plan, function);

    const phoenix::PartialRerunScopeResolver resolver;
    const auto result = resolver.resolve(request);

    return result.status == phoenix::PartialRerunScopeStatus::no_actor_subtree_rerun
        && !result.execution_request.has_value();
}

bool test_missing_actor_id_is_invalid()
{
    const auto function = make_actor_function("rerun_actor");
    const auto plan = make_plan(true, false);
    auto request = make_scope_request(plan, function);
    request.actor_id = std::nullopt;

    const phoenix::PartialRerunScopeResolver resolver;
    const auto result = resolver.resolve(request);

    return result.status == phoenix::PartialRerunScopeStatus::invalid_request
        && !result.execution_request.has_value();
}

bool test_resolved_request_can_drive_rerun_apply()
{
    auto scene = make_scene();
    const auto function = make_actor_function("rerun_actor");
    const auto plan = make_plan(true, false);
    const auto scope_request = make_scope_request(plan, function);

    const phoenix::PartialRerunScopeResolver resolver;
    const auto scope = resolver.resolve(scope_request);
    if (!scope.execution_request.has_value()) {
        return false;
    }

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
    const auto applied = applier.apply(phoenix::PartialRerunApplyRequest{
        &scene,
        &plan,
        nullptr,
        &executor,
        &*scope.execution_request,
    });

    return applied.status == phoenix::PartialRerunApplyStatus::applied_rerun_actor_subtree
        && scene.root.children.size() == 3
        && scene.root.children[0].id == "before"
        && scene.root.children[1].id == "target"
        && scene.root.children[1].children.empty()
        && scene.root.children[2].id == "after";
}

bool test_status_strings_are_stable()
{
    return std::string{phoenix::to_string(phoenix::PartialRerunScopeStatus::resolved)}
            == "resolved"
        && std::string{phoenix::to_string(phoenix::PartialRerunScopeStatus::invalid_request)}
            == "invalid_request"
        && std::string{phoenix::to_string(phoenix::PartialRerunScopeStatus::no_actor_subtree_rerun)}
            == "no_actor_subtree_rerun"
        && std::string{phoenix::to_string(phoenix::PartialRerunScopeStatus::cached_subtree_available)}
            == "cached_subtree_available";
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

    ok = run_test("resolves execution request for actor subtree cache miss", test_resolves_execution_request_for_actor_subtree_cache_miss) && ok;
    ok = run_test("cached actor subtree skips execution resolution", test_cached_actor_subtree_skips_execution_resolution) && ok;
    ok = run_test("non actor subtree plan has no actor rerun", test_non_actor_subtree_plan_has_no_actor_rerun) && ok;
    ok = run_test("missing actor id is invalid", test_missing_actor_id_is_invalid) && ok;
    ok = run_test("resolved request can drive rerun apply", test_resolved_request_can_drive_rerun_apply) && ok;
    ok = run_test("status strings are stable", test_status_strings_are_stable) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
