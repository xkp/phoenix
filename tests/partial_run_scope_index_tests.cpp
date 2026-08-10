#include "phoenix/partial_run_scope_index.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

phoenix::FunctionDescriptor make_function(const char* id, bool generates_actor)
{
    phoenix::FunctionDescriptor function;
    function.id = id;
    function.generates_actor = generates_actor;
    return function;
}

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
    scope.global_seed = 42;
    return scope;
}

bool input_has_geometry_label(
    const std::vector<phoenix::PortValue>& inputs,
    const char* port,
    const char* label)
{
    for (const auto& input : inputs) {
        if (input.port != port) {
            continue;
        }

        const auto* geometry = input.value.as_geometry();
        return geometry != nullptr && geometry->debug_label == label;
    }

    return false;
}

bool test_add_scope_preserves_order()
{
    const auto root_function = make_function("root", false);
    const auto child_function = make_function("child", true);

    phoenix::PartialRerunScopeIndex index;
    const auto root_index = index.add_scope(make_scope(root_function, {"root"}, "actor:root", false));
    const auto child_index = index.add_scope(make_scope(
        child_function,
        {"root", "2:child"},
        "actor:root:2:child",
        true,
        root_index));

    return root_index == 0
        && child_index == 1
        && index.scopes().size() == 2
        && index.scopes()[0].function_id == "root"
        && index.scopes()[1].function_id == "child";
}

bool test_find_by_actor_id()
{
    const auto root_function = make_function("root", false);
    const auto child_function = make_function("child", true);

    phoenix::PartialRerunScopeIndex index;
    const auto root_index = index.add_scope(make_scope(root_function, {"root"}, "actor:root", false));
    const auto child_index = index.add_scope(make_scope(
        child_function,
        {"root", "2:child"},
        "actor:child",
        true,
        root_index));

    const auto result = index.find_by_actor_id("actor:child");

    return result.status == phoenix::PartialRerunScopeLookupStatus::found
        && child_index == 1
        && result.scope_index.has_value()
        && *result.scope_index == child_index
        && result.scope != nullptr
        && result.scope->function_id == "child";
}

bool test_find_by_call_path()
{
    const auto root_function = make_function("root", false);
    const auto child_function = make_function("child", true);

    phoenix::PartialRerunScopeIndex index;
    const auto root_index = index.add_scope(make_scope(root_function, {"root"}, "actor:root", false));
    const auto child_index = index.add_scope(make_scope(
        child_function,
        {"root", "2:child"},
        "actor:child",
        true,
        root_index));

    const auto result = index.find_by_call_path({"root", "2:child"});

    return result.status == phoenix::PartialRerunScopeLookupStatus::found
        && result.scope != nullptr
        && result.scope_index.has_value()
        && *result.scope_index == child_index
        && result.scope->actor_id == "actor:child";
}

bool test_nearest_actor_scope_for_non_actor_nested_function()
{
    const auto root_function = make_function("root", false);
    const auto actor_function = make_function("floor", true);
    const auto helper_function = make_function("helper", false);

    phoenix::PartialRerunScopeIndex index;
    const auto root_index = index.add_scope(make_scope(root_function, {"root"}, "actor:root", false));
    const auto actor_index = index.add_scope(make_scope(
        actor_function,
        {"root", "2:floor"},
        "actor:floor",
        true,
        root_index));
    const auto helper_index = index.add_scope(make_scope(
        helper_function,
        {"root", "2:floor", "9:helper"},
        "actor:floor",
        false,
        actor_index));

    const auto result = index.find_nearest_actor_scope({"root", "2:floor", "9:helper"});

    return result.status == phoenix::PartialRerunScopeLookupStatus::found
        && helper_index == 2
        && result.scope_index.has_value()
        && *result.scope_index == actor_index
        && result.scope != nullptr
        && result.scope->function_id == "floor"
        && result.scope->actor_id == "actor:floor";
}

bool test_nearest_actor_scope_prefers_deepest_actor_boundary()
{
    const auto root_function = make_function("root", false);
    const auto floor_function = make_function("floor", true);
    const auto window_function = make_function("window", true);

    phoenix::PartialRerunScopeIndex index;
    const auto root_index = index.add_scope(make_scope(root_function, {"root"}, "actor:root", false));
    const auto floor_index = index.add_scope(make_scope(
        floor_function,
        {"root", "2:floor"},
        "actor:floor",
        true,
        root_index));
    const auto window_index = index.add_scope(make_scope(
        window_function,
        {"root", "2:floor", "5:window"},
        "actor:window",
        true,
        floor_index));

    const auto result = index.find_nearest_actor_scope({"root", "2:floor", "5:window"});

    return result.status == phoenix::PartialRerunScopeLookupStatus::found
        && result.scope_index.has_value()
        && *result.scope_index == window_index
        && result.scope != nullptr
        && result.scope->function_id == "window";
}

bool test_parent_actor_scope_climbs_to_nearest_parent_boundary()
{
    const auto root_function = make_function("root", false);
    const auto floor_function = make_function("floor", true);
    const auto helper_function = make_function("helper", false);
    const auto window_function = make_function("window", true);

    phoenix::PartialRerunScopeIndex index;
    const auto root_index = index.add_scope(make_scope(root_function, {"root"}, "actor:root", false));
    const auto floor_index = index.add_scope(make_scope(
        floor_function,
        {"root", "2:floor"},
        "actor:floor",
        true,
        root_index));
    const auto helper_index = index.add_scope(make_scope(
        helper_function,
        {"root", "2:floor", "4:helper"},
        "actor:floor",
        false,
        floor_index));
    const auto window_index = index.add_scope(make_scope(
        window_function,
        {"root", "2:floor", "4:helper", "6:window"},
        "actor:window",
        true,
        helper_index));

    const auto result = index.find_parent_actor_scope(window_index);

    return result.status == phoenix::PartialRerunScopeLookupStatus::found
        && result.scope_index.has_value()
        && *result.scope_index == floor_index
        && result.scope != nullptr
        && result.scope->actor_id == "actor:floor";
}

bool test_parent_actor_scope_reports_not_found_for_root()
{
    const auto root_function = make_function("root", false);

    phoenix::PartialRerunScopeIndex index;
    const auto root_index = index.add_scope(make_scope(root_function, {"root"}, "actor:root", false));
    const auto result = index.find_parent_actor_scope(root_index);

    return result.status == phoenix::PartialRerunScopeLookupStatus::not_found
        && !result.scope_index.has_value()
        && result.scope == nullptr;
}

bool test_duplicate_actor_id_is_ambiguous()
{
    const auto first_function = make_function("first", true);
    const auto second_function = make_function("second", true);

    phoenix::PartialRerunScopeIndex index;
    const auto first_index = index.add_scope(make_scope(
        first_function,
        {"root", "1:first"},
        "actor:duplicate",
        true));
    const auto second_index = index.add_scope(make_scope(
        second_function,
        {"root", "2:second"},
        "actor:duplicate",
        true));

    const auto result = index.find_by_actor_id("actor:duplicate");

    return first_index == 0
        && second_index == 1
        && result.status == phoenix::PartialRerunScopeLookupStatus::ambiguous
        && !result.scope_index.has_value()
        && result.scope == nullptr;
}

bool test_missing_and_invalid_lookup_statuses()
{
    const auto function = make_function("root", false);

    phoenix::PartialRerunScopeIndex index;
    const auto root_index = index.add_scope(make_scope(function, {"root"}, "actor:root", false));

    return root_index == 0
        && index.find_by_actor_id("missing").status == phoenix::PartialRerunScopeLookupStatus::not_found
        && index.find_by_actor_id("").status == phoenix::PartialRerunScopeLookupStatus::invalid_request
        && index.find_by_call_path({"missing"}).status == phoenix::PartialRerunScopeLookupStatus::not_found
        && index.find_by_call_path({}).status == phoenix::PartialRerunScopeLookupStatus::invalid_request
        && index.find_nearest_actor_scope({}).status == phoenix::PartialRerunScopeLookupStatus::invalid_request;
}

bool test_status_strings_are_stable()
{
    return std::string{phoenix::to_string(phoenix::PartialRerunScopeLookupStatus::found)}
            == "found"
        && std::string{phoenix::to_string(phoenix::PartialRerunScopeLookupStatus::not_found)}
            == "not_found"
        && std::string{phoenix::to_string(phoenix::PartialRerunScopeLookupStatus::ambiguous)}
            == "ambiguous"
        && std::string{phoenix::to_string(phoenix::PartialRerunScopeLookupStatus::invalid_request)}
            == "invalid_request";
}

bool test_executor_populates_scope_index_for_nested_calls()
{
    phoenix::FunctionDescriptor helper;
    helper.id = "helper";
    helper.instructions = {
        make_instruction(11, "helper_noop"),
    };

    phoenix::FunctionDescriptor child;
    child.id = "actor-child";
    child.generates_actor = true;
    auto call_helper = make_instruction(3, "call_helper");
    call_helper.called_function_id = helper.id;
    child.instructions = {call_helper};

    phoenix::FunctionDescriptor parent;
    parent.id = "parent";
    auto call_child = make_instruction(2, "call_child");
    call_child.called_function_id = child.id;
    parent.instructions = {call_child};

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "helper_noop",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{frame.inputs.node_id};
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);
    functions.register_function(helper);

    phoenix::PartialRerunScopeIndex scope_index;
    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 123;
    request.trace_level = phoenix::ExecutionTraceLevel::scope;
    request.scope_trace_sink = &scope_index;

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    const auto helper_lookup = scope_index.find_by_call_path({
        "root",
        "2:actor-child",
        "3:helper",
    });
    const auto owning_actor = scope_index.find_nearest_actor_scope({
        "root",
        "2:actor-child",
        "3:helper",
    });

    return result.status == phoenix::FunctionExecutionStatus::completed
        && scope_index.scopes().size() == 3
        && scope_index.scopes()[0].function_id == "parent"
        && scope_index.scopes()[0].actor_id == "actor:root"
        && !scope_index.scopes()[0].parent_scope_index.has_value()
        && scope_index.scopes()[1].function_id == "actor-child"
        && scope_index.scopes()[1].actor_id == "actor:root:2:actor-child"
        && scope_index.scopes()[1].parent_scope_index.has_value()
        && *scope_index.scopes()[1].parent_scope_index == 0
        && scope_index.scopes()[1].caller_node_id.has_value()
        && *scope_index.scopes()[1].caller_node_id == 2
        && scope_index.scopes()[2].function_id == "helper"
        && scope_index.scopes()[2].actor_id == "actor:root:2:actor-child"
        && scope_index.scopes()[2].parent_scope_index.has_value()
        && *scope_index.scopes()[2].parent_scope_index == 1
        && helper_lookup.status == phoenix::PartialRerunScopeLookupStatus::found
        && owning_actor.status == phoenix::PartialRerunScopeLookupStatus::found
        && owning_actor.scope_index.has_value()
        && *owning_actor.scope_index == 1;
}

bool test_executor_scope_index_records_multiplexed_actor_items()
{
    phoenix::FunctionDescriptor child;
    child.id = "actor-item";
    child.input_ports = {make_input_port("input", "geometry")};
    child.generates_actor = true;
    child.instructions = {
        make_instruction(
            7,
            "capture_item",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
    };

    phoenix::FunctionDescriptor parent;
    parent.id = "parent";
    parent.input_ports = {make_input_port("input", "geometry")};
    auto call_child = make_instruction(
        2,
        "call_child",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    parent.instructions = {call_child};

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "capture_item",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{frame.inputs.node_id};
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    phoenix::PartialRerunScopeIndex scope_index;
    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};
    request.trace_level = phoenix::ExecutionTraceLevel::scope;
    request.scope_trace_sink = &scope_index;

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && scope_index.scopes().size() == 3
        && scope_index.scopes()[1].call_path == phoenix::FunctionCallPath({"root", "2:actor-item", "item:0"})
        && scope_index.scopes()[1].actor_id == "actor:root:2:actor-item:item:0"
        && input_has_geometry_label(scope_index.scopes()[1].inputs, "input", "face-a")
        && scope_index.scopes()[2].call_path == phoenix::FunctionCallPath({"root", "2:actor-item", "item:1"})
        && scope_index.scopes()[2].actor_id == "actor:root:2:actor-item:item:1"
        && input_has_geometry_label(scope_index.scopes()[2].inputs, "input", "face-b");
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

    ok = run_test("add scope preserves order", test_add_scope_preserves_order) && ok;
    ok = run_test("find by actor id", test_find_by_actor_id) && ok;
    ok = run_test("find by call path", test_find_by_call_path) && ok;
    ok = run_test("nearest actor scope for non actor nested function", test_nearest_actor_scope_for_non_actor_nested_function) && ok;
    ok = run_test("nearest actor scope prefers deepest actor boundary", test_nearest_actor_scope_prefers_deepest_actor_boundary) && ok;
    ok = run_test("parent actor scope climbs to nearest parent boundary", test_parent_actor_scope_climbs_to_nearest_parent_boundary) && ok;
    ok = run_test("parent actor scope reports not found for root", test_parent_actor_scope_reports_not_found_for_root) && ok;
    ok = run_test("duplicate actor id is ambiguous", test_duplicate_actor_id_is_ambiguous) && ok;
    ok = run_test("missing and invalid lookup statuses", test_missing_and_invalid_lookup_statuses) && ok;
    ok = run_test("status strings are stable", test_status_strings_are_stable) && ok;
    ok = run_test("executor populates scope index for nested calls", test_executor_populates_scope_index_for_nested_calls) && ok;
    ok = run_test("executor scope index records multiplexed actor items", test_executor_scope_index_records_multiplexed_actor_items) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
