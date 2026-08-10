#include "phoenix/graph.hpp"
#include "phoenix/invalidation.hpp"

#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using phoenix::EdgeDescriptor;
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

InstructionDescriptor make_instruction(
    phoenix::NodeId id,
    const char* kind,
    std::vector<PortDescriptor> inputs,
    std::vector<PortDescriptor> outputs)
{
    InstructionDescriptor instruction;
    instruction.id = id;
    instruction.kind = kind;
    instruction.input_ports = std::move(inputs);
    instruction.output_ports = std::move(outputs);
    instruction.has_else_port = false;
    return instruction;
}

bool dirty_equals(
    const phoenix::InvalidationResult& result,
    const std::vector<phoenix::NodeId>& expected)
{
    return result.dirty_instructions == expected;
}

bool has_reason(
    const phoenix::InvalidationResult& result,
    phoenix::InvalidationReason expected)
{
    for (const auto reason : result.reasons) {
        if (reason == expected) {
            return true;
        }
    }

    return false;
}

FunctionDescriptor make_branching_function()
{
    FunctionDescriptor function;
    function.id = "branching";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            1,
            "source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            2,
            "left",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            3,
            "right",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            4,
            "join",
            {make_input_port("left", "geometry"), make_input_port("right", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {
        EdgeDescriptor{1, "output", 2, "input"},
        EdgeDescriptor{1, "output", 3, "input"},
        EdgeDescriptor{2, "output", 4, "left"},
        EdgeDescriptor{3, "output", 4, "right"},
        EdgeDescriptor{4, "output", 99, "result"},
    };
    function.output_node_id = 99;
    return function;
}

FunctionDescriptor make_leaf_function()
{
    auto function = make_branching_function();
    function.instructions.push_back(make_instruction(
        50,
        "leaf",
        {},
        {make_output_port("output", "geometry")}));
    return function;
}

bool test_downstream_dirty_expansion()
{
    const auto function = make_branching_function();
    const phoenix::InvalidationPlanner planner;

    phoenix::InvalidationRequest request;
    request.function = &function;
    request.changed_instructions = {2};

    const auto result = planner.plan(request);
    return dirty_equals(result, {2, 4, 99})
        && has_reason(result, phoenix::InvalidationReason::instruction_dirty)
        && has_reason(result, phoenix::InvalidationReason::function_outputs_affected)
        && has_reason(result, phoenix::InvalidationReason::parent_propagation_required)
        && result.function_outputs_affected
        && result.parent_propagation_required
        && !result.actor_subtree_affected;
}

bool test_branch_change_does_not_dirty_sibling_branch()
{
    const auto function = make_branching_function();
    const phoenix::InvalidationPlanner planner;

    phoenix::InvalidationRequest request;
    request.function = &function;
    request.changed_instructions = {3};

    const auto result = planner.plan(request);
    return dirty_equals(result, {3, 4, 99});
}

bool test_multiple_changed_instructions_merge_dirty_sets()
{
    const auto function = make_branching_function();
    const phoenix::InvalidationPlanner planner;

    phoenix::InvalidationRequest request;
    request.function = &function;
    request.changed_instructions = {2, 3};

    const auto result = planner.plan(request);
    return dirty_equals(result, {2, 3, 4, 99})
        && result.function_outputs_affected
        && result.parent_propagation_required;
}

bool test_leaf_change_does_not_require_parent_propagation()
{
    const auto function = make_leaf_function();
    const phoenix::InvalidationPlanner planner;

    phoenix::InvalidationRequest request;
    request.function = &function;
    request.changed_instructions = {50};

    const auto result = planner.plan(request);
    return dirty_equals(result, {50})
        && has_reason(result, phoenix::InvalidationReason::instruction_dirty)
        && !has_reason(result, phoenix::InvalidationReason::function_outputs_affected)
        && !has_reason(result, phoenix::InvalidationReason::parent_propagation_required)
        && !result.function_outputs_affected
        && !result.parent_propagation_required;
}

bool test_actor_instruction_marks_actor_subtree()
{
    auto function = make_branching_function();
    function.instructions[1].generates_actor = true;

    const phoenix::InvalidationPlanner planner;
    phoenix::InvalidationRequest request;
    request.function = &function;
    request.changed_instructions = {2};

    const auto result = planner.plan(request);
    return result.actor_subtree_affected
        && has_reason(result, phoenix::InvalidationReason::actor_subtree_affected)
        && result.function_outputs_affected;
}

bool test_actor_function_marks_actor_subtree_even_for_non_actor_instruction()
{
    auto function = make_branching_function();
    function.generates_actor = true;

    const phoenix::InvalidationPlanner planner;
    phoenix::InvalidationRequest request;
    request.function = &function;
    request.changed_instructions = {3};

    const auto result = planner.plan(request);
    return result.actor_subtree_affected
        && result.function_outputs_affected;
}

bool test_invalid_changed_instruction_is_ignored()
{
    const auto function = make_branching_function();

    const phoenix::InvalidationPlanner planner;
    phoenix::InvalidationRequest request;
    request.function = &function;
    request.changed_instructions = {1000};

    const auto result = planner.plan(request);
    return result.dirty_instructions.empty()
        && !result.function_outputs_affected
        && !result.actor_subtree_affected
        && !result.parent_propagation_required;
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

    ok = run_test("downstream dirty expansion", test_downstream_dirty_expansion) && ok;
    ok = run_test("branch change does not dirty sibling branch", test_branch_change_does_not_dirty_sibling_branch) && ok;
    ok = run_test("multiple changed instructions merge dirty sets", test_multiple_changed_instructions_merge_dirty_sets) && ok;
    ok = run_test("leaf change does not require parent propagation", test_leaf_change_does_not_require_parent_propagation) && ok;
    ok = run_test("actor instruction marks actor subtree", test_actor_instruction_marks_actor_subtree) && ok;
    ok = run_test("actor function marks actor subtree even for non-actor instruction", test_actor_function_marks_actor_subtree_even_for_non_actor_instruction) && ok;
    ok = run_test("invalid changed instruction is ignored", test_invalid_changed_instruction_is_ignored) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
