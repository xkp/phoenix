#include "phoenix/partial_run_dirty_instruction_discovery.hpp"

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

phoenix::FunctionDescriptor make_actor_function(const char* id)
{
    phoenix::FunctionDescriptor function;
    function.id = id;
    function.generates_actor = true;
    function.input_ports = {make_input_port("input", "geometry")};
    return function;
}

phoenix::PartialRerunPlan make_plan(
    bool actor_subtree_affected,
    bool cache_hit,
    bool parent_propagation_required = false)
{
    phoenix::PartialRerunPlan plan;
    plan.invalidation.actor_subtree_affected = actor_subtree_affected;
    plan.invalidation.parent_propagation_required = parent_propagation_required;
    plan.actor_subtree_key = phoenix::CacheKey{"actor-subtree-key"};
    plan.actor_subtree_cache_hit = cache_hit;
    return plan;
}

phoenix::PartialRerunScopeRecord make_scope(
    const phoenix::FunctionDescriptor& function,
    phoenix::FunctionCallPath call_path,
    const char* actor_id,
    std::optional<std::size_t> parent_scope_index = std::nullopt)
{
    phoenix::PartialRerunScopeRecord scope;
    scope.function_id = function.id;
    scope.function = &function;
    scope.call_path = std::move(call_path);
    scope.actor_id = actor_id;
    scope.generates_actor = true;
    scope.parent_scope_index = parent_scope_index;
    scope.inputs = {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry("input")}};
    scope.global_seed = 99;
    return scope;
}

void record_instruction(
    phoenix::PartialRerunInstructionIndex& index,
    const char* function_id,
    phoenix::NodeId node_id,
    phoenix::FunctionCallPath call_path)
{
    phoenix::FunctionExecutionInstructionRecord record;
    record.function_id = function_id;
    record.node_id = node_id;
    record.call_path = std::move(call_path);
    record.instruction_kind = "dirty_step";
    index.record_instruction(std::move(record));
}

bool test_discovers_one_scope_from_dirty_function_node()
{
    const auto actor = make_actor_function("floor");
    phoenix::PartialRerunScopeIndex scope_index;
    const auto scope_record_index =
        scope_index.add_scope(make_scope(actor, {"root", "2:floor"}, "actor:floor"));

    phoenix::PartialRerunInstructionIndex instruction_index;
    record_instruction(instruction_index, "helper", 7, {"root", "2:floor", "9:helper"});

    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunDirtyInstructionDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunDirtyInstructionDiscoveryRequest{
        &plan,
        &instruction_index,
        &scope_index,
        "helper",
        7,
    });

    return result.status == phoenix::PartialRerunDirtyInstructionDiscoveryStatus::discovered
        && result.discoveries.size() == 1
        && result.scope_requests.size() == 1
        && result.scope_requests.front().function == &actor
        && result.scope_requests.front().context.call_path
            == phoenix::FunctionCallPath({"root", "2:floor"})
        && result.scope_requests.front().actor_id.has_value()
        && *result.scope_requests.front().actor_id == "actor:floor"
        && scope_record_index == 0;
}

bool test_multiple_executed_paths_return_multiple_scope_requests()
{
    const auto actor = make_actor_function("window");
    phoenix::PartialRerunScopeIndex scope_index;
    const auto first_scope_index =
        scope_index.add_scope(make_scope(actor, {"root", "2:window"}, "actor:first"));
    const auto second_scope_index =
        scope_index.add_scope(make_scope(actor, {"root", "3:window"}, "actor:second"));

    phoenix::PartialRerunInstructionIndex instruction_index;
    record_instruction(instruction_index, "window", 7, {"root", "2:window"});
    record_instruction(instruction_index, "window", 7, {"root", "3:window"});

    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunDirtyInstructionDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunDirtyInstructionDiscoveryRequest{
        &plan,
        &instruction_index,
        &scope_index,
        "window",
        7,
    });

    return result.status == phoenix::PartialRerunDirtyInstructionDiscoveryStatus::discovered
        && result.discoveries.size() == 2
        && result.scope_requests.size() == 2
        && result.scope_requests[0].actor_id.has_value()
        && *result.scope_requests[0].actor_id == "actor:first"
        && result.scope_requests[1].actor_id.has_value()
        && *result.scope_requests[1].actor_id == "actor:second"
        && first_scope_index == 0
        && second_scope_index == 1;
}

bool test_duplicate_instruction_traces_are_deduped_by_call_path()
{
    const auto actor = make_actor_function("window");
    phoenix::PartialRerunScopeIndex scope_index;
    const auto scope_record_index =
        scope_index.add_scope(make_scope(actor, {"root", "2:window"}, "actor:window"));

    phoenix::PartialRerunInstructionIndex instruction_index;
    record_instruction(instruction_index, "window", 7, {"root", "2:window"});
    record_instruction(instruction_index, "window", 7, {"root", "2:window"});

    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunDirtyInstructionDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunDirtyInstructionDiscoveryRequest{
        &plan,
        &instruction_index,
        &scope_index,
        "window",
        7,
    });

    return result.status == phoenix::PartialRerunDirtyInstructionDiscoveryStatus::discovered
        && result.discoveries.size() == 1
        && result.scope_requests.size() == 1
        && scope_record_index == 0;
}

bool test_partial_discovery_keeps_successful_scopes()
{
    const auto actor = make_actor_function("window");
    phoenix::PartialRerunScopeIndex scope_index;
    const auto scope_record_index =
        scope_index.add_scope(make_scope(actor, {"root", "2:window"}, "actor:window"));

    phoenix::PartialRerunInstructionIndex instruction_index;
    record_instruction(instruction_index, "window", 7, {"root", "2:window"});
    record_instruction(instruction_index, "window", 7, {"root", "3:window"});

    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunDirtyInstructionDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunDirtyInstructionDiscoveryRequest{
        &plan,
        &instruction_index,
        &scope_index,
        "window",
        7,
    });

    return result.status == phoenix::PartialRerunDirtyInstructionDiscoveryStatus::partially_discovered
        && result.discoveries.size() == 2
        && result.scope_requests.size() == 1
        && result.discoveries[1].status == phoenix::PartialRerunScopeDiscoveryStatus::scope_not_found
        && scope_record_index == 0;
}

bool test_parent_propagation_promotes_dirty_child_to_parent_actor_scope()
{
    const auto floor = make_actor_function("floor");
    const auto window = make_actor_function("window");
    phoenix::PartialRerunScopeIndex scope_index;
    const auto floor_scope_index =
        scope_index.add_scope(make_scope(floor, {"root", "2:floor"}, "actor:floor"));
    const auto window_scope_index = scope_index.add_scope(make_scope(
        window,
        {"root", "2:floor", "5:window"},
        "actor:window",
        floor_scope_index));

    phoenix::PartialRerunInstructionIndex instruction_index;
    record_instruction(instruction_index, "window", 7, {"root", "2:floor", "5:window"});

    const auto plan = make_plan(true, false, true);
    const phoenix::PartialRerunDirtyInstructionDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunDirtyInstructionDiscoveryRequest{
        &plan,
        &instruction_index,
        &scope_index,
        "window",
        7,
    });

    return result.status == phoenix::PartialRerunDirtyInstructionDiscoveryStatus::discovered
        && result.scope_requests.size() == 1
        && result.scope_requests.front().function == &floor
        && result.scope_requests.front().actor_id.has_value()
        && *result.scope_requests.front().actor_id == "actor:floor"
        && floor_scope_index == 0
        && window_scope_index == 1;
}

bool test_no_instruction_traces()
{
    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunInstructionIndex instruction_index;
    const phoenix::PartialRerunScopeIndex scope_index;
    const phoenix::PartialRerunDirtyInstructionDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunDirtyInstructionDiscoveryRequest{
        &plan,
        &instruction_index,
        &scope_index,
        "window",
        7,
    });

    return result.status == phoenix::PartialRerunDirtyInstructionDiscoveryStatus::no_instruction_traces
        && result.discoveries.empty()
        && result.scope_requests.empty();
}

bool test_cached_and_non_actor_plans_short_circuit()
{
    const phoenix::PartialRerunInstructionIndex instruction_index;
    const phoenix::PartialRerunScopeIndex scope_index;
    const phoenix::PartialRerunDirtyInstructionDiscovery discovery;

    const auto cached_plan = make_plan(true, true);
    const auto cached = discovery.discover(phoenix::PartialRerunDirtyInstructionDiscoveryRequest{
        &cached_plan,
        &instruction_index,
        &scope_index,
        "window",
        7,
    });

    const auto non_actor_plan = make_plan(false, false);
    const auto non_actor = discovery.discover(phoenix::PartialRerunDirtyInstructionDiscoveryRequest{
        &non_actor_plan,
        &instruction_index,
        &scope_index,
        "window",
        7,
    });

    return cached.status == phoenix::PartialRerunDirtyInstructionDiscoveryStatus::cached_subtree_available
        && non_actor.status == phoenix::PartialRerunDirtyInstructionDiscoveryStatus::no_actor_subtree_rerun;
}

bool test_missing_scope_reports_not_found()
{
    phoenix::PartialRerunInstructionIndex instruction_index;
    record_instruction(instruction_index, "window", 7, {"root", "2:window"});

    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunScopeIndex scope_index;
    const phoenix::PartialRerunDirtyInstructionDiscovery discovery;
    const auto result = discovery.discover(phoenix::PartialRerunDirtyInstructionDiscoveryRequest{
        &plan,
        &instruction_index,
        &scope_index,
        "window",
        7,
    });

    return result.status == phoenix::PartialRerunDirtyInstructionDiscoveryStatus::scope_not_found
        && result.discoveries.size() == 1
        && result.scope_requests.empty();
}

bool test_invalid_request()
{
    const auto plan = make_plan(true, false);
    const phoenix::PartialRerunInstructionIndex instruction_index;
    const phoenix::PartialRerunScopeIndex scope_index;
    const phoenix::PartialRerunDirtyInstructionDiscovery discovery;

    const auto no_plan = discovery.discover(phoenix::PartialRerunDirtyInstructionDiscoveryRequest{
        nullptr,
        &instruction_index,
        &scope_index,
        "window",
        7,
    });
    const auto no_instruction_index = discovery.discover(
        phoenix::PartialRerunDirtyInstructionDiscoveryRequest{
            &plan,
            nullptr,
            &scope_index,
            "window",
            7,
        });
    const auto no_function = discovery.discover(phoenix::PartialRerunDirtyInstructionDiscoveryRequest{
        &plan,
        &instruction_index,
        &scope_index,
        "",
        7,
    });

    return no_plan.status == phoenix::PartialRerunDirtyInstructionDiscoveryStatus::invalid_request
        && no_instruction_index.status
            == phoenix::PartialRerunDirtyInstructionDiscoveryStatus::invalid_request
        && no_function.status == phoenix::PartialRerunDirtyInstructionDiscoveryStatus::invalid_request;
}

bool test_status_strings_are_stable()
{
    return std::string{phoenix::to_string(
               phoenix::PartialRerunDirtyInstructionDiscoveryStatus::discovered)}
            == "discovered"
        && std::string{phoenix::to_string(
               phoenix::PartialRerunDirtyInstructionDiscoveryStatus::partially_discovered)}
            == "partially_discovered"
        && std::string{phoenix::to_string(
               phoenix::PartialRerunDirtyInstructionDiscoveryStatus::invalid_request)}
            == "invalid_request"
        && std::string{phoenix::to_string(
               phoenix::PartialRerunDirtyInstructionDiscoveryStatus::no_actor_subtree_rerun)}
            == "no_actor_subtree_rerun"
        && std::string{phoenix::to_string(
               phoenix::PartialRerunDirtyInstructionDiscoveryStatus::cached_subtree_available)}
            == "cached_subtree_available"
        && std::string{phoenix::to_string(
               phoenix::PartialRerunDirtyInstructionDiscoveryStatus::no_instruction_traces)}
            == "no_instruction_traces"
        && std::string{phoenix::to_string(
               phoenix::PartialRerunDirtyInstructionDiscoveryStatus::scope_not_found)}
            == "scope_not_found"
        && std::string{phoenix::to_string(
               phoenix::PartialRerunDirtyInstructionDiscoveryStatus::scope_ambiguous)}
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

    ok = run_test("discovers one scope from dirty function node", test_discovers_one_scope_from_dirty_function_node) && ok;
    ok = run_test("multiple executed paths return multiple scope requests", test_multiple_executed_paths_return_multiple_scope_requests) && ok;
    ok = run_test("duplicate instruction traces are deduped by call path", test_duplicate_instruction_traces_are_deduped_by_call_path) && ok;
    ok = run_test("partial discovery keeps successful scopes", test_partial_discovery_keeps_successful_scopes) && ok;
    ok = run_test("parent propagation promotes dirty child to parent actor scope", test_parent_propagation_promotes_dirty_child_to_parent_actor_scope) && ok;
    ok = run_test("no instruction traces", test_no_instruction_traces) && ok;
    ok = run_test("cached and non actor plans short circuit", test_cached_and_non_actor_plans_short_circuit) && ok;
    ok = run_test("missing scope reports not found", test_missing_scope_reports_not_found) && ok;
    ok = run_test("invalid request", test_invalid_request) && ok;
    ok = run_test("status strings are stable", test_status_strings_are_stable) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
