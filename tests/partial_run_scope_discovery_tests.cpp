#include "phoenix/partial_run_scope_discovery.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

phoenix::PortDescriptor make_input_port(const char* id, const char* type)
{
    return phoenix::PortDescriptor{id, type, phoenix::PortDirection::input};
}

phoenix::PortDescriptor make_output_port(const char* id, const char* type)
{
    return phoenix::PortDescriptor{id, type, phoenix::PortDirection::output};
}

phoenix::InstructionDescriptor make_instruction(
    phoenix::NodeId id,
    const char* kind,
    std::vector<phoenix::PortDescriptor> inputs = {},
    std::vector<phoenix::PortDescriptor> outputs = {})
{
    phoenix::InstructionDescriptor instruction;
    instruction.id = id;
    instruction.kind = kind;
    instruction.input_ports = std::move(inputs);
    instruction.output_ports = std::move(outputs);
    instruction.has_else_port = false;
    return instruction;
}

phoenix::PartialRerunPlan make_plan(bool actor_subtree_affected, bool cache_hit)
{
    phoenix::PartialRerunPlan plan;
    plan.invalidation.actor_subtree_affected = actor_subtree_affected;
    plan.actor_subtree_key = phoenix::CacheKey{"actor-subtree-key"};
    plan.actor_subtree_cache_hit = cache_hit;
    return plan;
}

phoenix::FunctionDescriptor make_actor_function(const char* id)
{
    phoenix::FunctionDescriptor function;
    function.id = id;
    function.input_ports = {make_input_port("input", "geometry")};
    function.generates_actor = true;
    function.instructions = {
        make_instruction(
            7,
            "actor_body",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
    };
    return function;
}

phoenix::PartialRerunScopeRecord make_scope(
    const phoenix::FunctionDescriptor& function,
    phoenix::FunctionCallPath call_path,
    const char* actor_id,
    bool generates_actor,
    std::optional<std::size_t> parent_scope_index = std::nullopt)
{
    phoenix::PartialRerunScopeRecord scope;
    scope.function_id = function.id;
    scope.function = &function;
    scope.call_path = std::move(call_path);
    scope.actor_id = actor_id;
    scope.generates_actor = generates_actor;
    scope.parent_scope_index = parent_scope_index;
    scope.inputs = {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry("scope-input")}};
    scope.global_seed = 55;
    return scope;
}

bool test_discovers_actor_scope_from_dirty_call_path()
{
    const auto function = make_actor_function("floor");
    phoenix::PartialRerunScopeIndex index;
    const auto scope_index = index.add_scope(make_scope(
        function,
        {"root", "2:floor"},
        "actor:floor",
        true));

    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunScopeDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunScopeDiscoveryRequest{
        &plan,
        &index,
        {"root", "2:floor", "9:helper"},
    });

    return result.status == phoenix::PartialRerunScopeDiscoveryStatus::discovered
        && result.lookup_status.has_value()
        && *result.lookup_status == phoenix::PartialRerunScopeLookupStatus::found
        && result.scope_request.has_value()
        && result.scope_request->plan == &plan
        && result.scope_request->function == &function
        && result.scope_request->context.function_id == "floor"
        && result.scope_request->context.call_path == phoenix::FunctionCallPath({"root", "2:floor"})
        && result.scope_request->context.global_seed == 55
        && result.scope_request->actor_id.has_value()
        && *result.scope_request->actor_id == "actor:floor"
        && scope_index == 0;
}

bool test_discovered_scope_can_resolve_execution_request()
{
    const auto function = make_actor_function("floor");
    phoenix::PartialRerunScopeIndex index;
    const auto scope_index = index.add_scope(make_scope(
        function,
        {"root", "2:floor"},
        "actor:floor",
        true));

    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunScopeDiscovery discovery;
    const auto discovered = discovery.discover(phoenix::PartialRerunScopeDiscoveryRequest{
        &plan,
        &index,
        {"root", "2:floor", "9:helper"},
    });
    if (!discovered.scope_request.has_value()) {
        return false;
    }

    const phoenix::PartialRerunScopeResolver resolver;
    const auto resolved = resolver.resolve(*discovered.scope_request);

    const auto* frame = resolved.execution_request.has_value()
        ? resolved.execution_request->call_stack.current()
        : nullptr;

    return resolved.status == phoenix::PartialRerunScopeStatus::resolved
        && resolved.execution_request.has_value()
        && resolved.execution_request->function == &function
        && scope_index == 0
        && frame != nullptr
        && frame->actor_id.has_value()
        && *frame->actor_id == "actor:floor";
}

bool test_parent_propagation_discovers_parent_actor_scope()
{
    const auto floor_function = make_actor_function("floor");
    const auto window_function = make_actor_function("window");

    phoenix::PartialRerunScopeIndex index;
    const auto floor_index = index.add_scope(make_scope(
        floor_function,
        {"root", "2:floor"},
        "actor:floor",
        true));
    const auto window_index = index.add_scope(make_scope(
        window_function,
        {"root", "2:floor", "5:window"},
        "actor:window",
        true,
        floor_index));

    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunScopeDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunScopeDiscoveryRequest{
        &plan,
        &index,
        {"root", "2:floor", "5:window"},
        true,
    });

    return result.status == phoenix::PartialRerunScopeDiscoveryStatus::discovered
        && result.scope_request.has_value()
        && result.scope_request->function == &floor_function
        && result.scope_request->actor_id.has_value()
        && *result.scope_request->actor_id == "actor:floor"
        && floor_index == 0
        && window_index == 1;
}

bool test_cached_plan_skips_discovery()
{
    const auto function = make_actor_function("floor");
    phoenix::PartialRerunScopeIndex index;
    const auto scope_index = index.add_scope(make_scope(
        function,
        {"root", "2:floor"},
        "actor:floor",
        true));

    const auto plan = make_plan(true, true);
    const phoenix::PartialRerunScopeDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunScopeDiscoveryRequest{
        &plan,
        &index,
        {"root", "2:floor"},
    });

    return result.status == phoenix::PartialRerunScopeDiscoveryStatus::cached_subtree_available
        && scope_index == 0
        && !result.scope_request.has_value();
}

bool test_non_actor_plan_skips_discovery()
{
    const auto function = make_actor_function("floor");
    phoenix::PartialRerunScopeIndex index;
    const auto scope_index = index.add_scope(make_scope(
        function,
        {"root", "2:floor"},
        "actor:floor",
        true));

    const auto plan = make_plan(false, false);
    const phoenix::PartialRerunScopeDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunScopeDiscoveryRequest{
        &plan,
        &index,
        {"root", "2:floor"},
    });

    return result.status == phoenix::PartialRerunScopeDiscoveryStatus::no_actor_subtree_rerun
        && scope_index == 0
        && !result.scope_request.has_value();
}

bool test_missing_scope_reports_not_found()
{
    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunScopeIndex index;
    const phoenix::PartialRerunScopeDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunScopeDiscoveryRequest{
        &plan,
        &index,
        {"root", "2:floor"},
    });

    return result.status == phoenix::PartialRerunScopeDiscoveryStatus::scope_not_found
        && result.lookup_status.has_value()
        && *result.lookup_status == phoenix::PartialRerunScopeLookupStatus::not_found
        && !result.scope_request.has_value();
}

bool test_invalid_scope_record_reports_invalid_request()
{
    const auto function = make_actor_function("floor");
    auto scope = make_scope(function, {"root", "2:floor"}, "actor:floor", true);
    scope.function = nullptr;

    phoenix::PartialRerunScopeIndex index;
    const auto scope_index = index.add_scope(scope);

    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunScopeDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunScopeDiscoveryRequest{
        &plan,
        &index,
        {"root", "2:floor"},
    });

    return result.status == phoenix::PartialRerunScopeDiscoveryStatus::invalid_request
        && scope_index == 0
        && result.lookup_status.has_value()
        && *result.lookup_status == phoenix::PartialRerunScopeLookupStatus::found
        && !result.scope_request.has_value();
}

bool test_invalid_request()
{
    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunScopeIndex index;
    const phoenix::PartialRerunScopeDiscovery discovery;

    const auto no_plan = discovery.discover(phoenix::PartialRerunScopeDiscoveryRequest{
        nullptr,
        &index,
        {"root"},
    });
    const auto no_index = discovery.discover(phoenix::PartialRerunScopeDiscoveryRequest{
        &plan,
        nullptr,
        {"root"},
    });
    const auto no_path = discovery.discover(phoenix::PartialRerunScopeDiscoveryRequest{
        &plan,
        &index,
        {},
    });

    return no_plan.status == phoenix::PartialRerunScopeDiscoveryStatus::invalid_request
        && no_index.status == phoenix::PartialRerunScopeDiscoveryStatus::invalid_request
        && no_path.status == phoenix::PartialRerunScopeDiscoveryStatus::invalid_request;
}

bool test_status_strings_are_stable()
{
    return std::string{phoenix::to_string(phoenix::PartialRerunScopeDiscoveryStatus::discovered)}
            == "discovered"
        && std::string{phoenix::to_string(phoenix::PartialRerunScopeDiscoveryStatus::invalid_request)}
            == "invalid_request"
        && std::string{phoenix::to_string(phoenix::PartialRerunScopeDiscoveryStatus::no_actor_subtree_rerun)}
            == "no_actor_subtree_rerun"
        && std::string{phoenix::to_string(phoenix::PartialRerunScopeDiscoveryStatus::cached_subtree_available)}
            == "cached_subtree_available"
        && std::string{phoenix::to_string(phoenix::PartialRerunScopeDiscoveryStatus::scope_not_found)}
            == "scope_not_found"
        && std::string{phoenix::to_string(phoenix::PartialRerunScopeDiscoveryStatus::scope_ambiguous)}
            == "scope_ambiguous";
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

    ok = run_test("discovers actor scope from dirty call path", test_discovers_actor_scope_from_dirty_call_path) && ok;
    ok = run_test("discovered scope can resolve execution request", test_discovered_scope_can_resolve_execution_request) && ok;
    ok = run_test("parent propagation discovers parent actor scope", test_parent_propagation_discovers_parent_actor_scope) && ok;
    ok = run_test("cached plan skips discovery", test_cached_plan_skips_discovery) && ok;
    ok = run_test("non actor plan skips discovery", test_non_actor_plan_skips_discovery) && ok;
    ok = run_test("missing scope reports not found", test_missing_scope_reports_not_found) && ok;
    ok = run_test("invalid scope record reports invalid request", test_invalid_scope_record_reports_invalid_request) && ok;
    ok = run_test("invalid request", test_invalid_request) && ok;
    ok = run_test("status strings are stable", test_status_strings_are_stable) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
