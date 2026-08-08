#include "phoenix/graph.hpp"

#include <cstdlib>
#include <iostream>

namespace {

using phoenix::EdgeDescriptor;
using phoenix::FunctionDescriptor;
using phoenix::GraphValidationCode;
using phoenix::GraphValidator;
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
    instruction.has_else_port = true;
    return instruction;
}

bool contains_issue(
    const phoenix::GraphValidationResult& result,
    GraphValidationCode code)
{
    for (const auto& issue : result.issues) {
        if (issue.code == code) {
            return true;
        }
    }

    return false;
}

bool test_valid_graph()
{
    FunctionDescriptor function;
    function.id = "valid";
    function.instructions = {
        make_instruction(
            1,
            "producer",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
        make_instruction(
            2,
            "consumer",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
    };
    function.edges = {
        EdgeDescriptor{1, "output", 2, "input"},
    };

    const GraphValidator validator;
    const auto result = validator.validate(function);
    return result.ok();
}

bool test_duplicate_instruction_id()
{
    FunctionDescriptor function;
    function.id = "duplicate-id";
    function.instructions = {
        make_instruction(
            1,
            "a",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
        make_instruction(
            1,
            "b",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
    };

    const GraphValidator validator;
    const auto result = validator.validate(function);
    return contains_issue(result, GraphValidationCode::duplicate_instruction_id);
}

bool test_duplicate_function_input_port()
{
    FunctionDescriptor function;
    function.id = "duplicate-function-port";
    function.input_ports = {
        make_input_port("input", "geometry"),
        make_input_port("input", "geometry"),
    };
    function.instructions = {
        make_instruction(
            1,
            "producer",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
    };

    const GraphValidator validator;
    const auto result = validator.validate(function);
    return contains_issue(result, GraphValidationCode::duplicate_function_input_port);
}

bool test_missing_target_port()
{
    FunctionDescriptor function;
    function.id = "missing-port";
    function.instructions = {
        make_instruction(
            1,
            "producer",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
        make_instruction(
            2,
            "consumer",
            {make_input_port("different_input", "geometry")},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
    };
    function.edges = {
        EdgeDescriptor{1, "output", 2, "input"},
    };

    const GraphValidator validator;
    const auto result = validator.validate(function);
    return contains_issue(result, GraphValidationCode::edge_references_missing_target_port);
}

bool test_missing_else_port()
{
    FunctionDescriptor function;
    function.id = "missing-else";

    auto instruction = make_instruction(
        1,
        "producer",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry")});
    instruction.has_else_port = true;

    function.instructions = {instruction};

    const GraphValidator validator;
    const auto result = validator.validate(function);
    return contains_issue(result, GraphValidationCode::instruction_missing_else_port);
}

bool test_wrong_instruction_input_port_direction()
{
    FunctionDescriptor function;
    function.id = "wrong-input-direction";

    auto instruction = make_instruction(
        1,
        "producer",
        {make_output_port("input", "geometry")},
        {make_output_port("output", "geometry"), make_output_port("else", "geometry")});

    function.instructions = {instruction};

    const GraphValidator validator;
    const auto result = validator.validate(function);
    return contains_issue(result, GraphValidationCode::instruction_input_port_wrong_direction);
}

bool test_type_mismatch()
{
    FunctionDescriptor function;
    function.id = "type-mismatch";
    function.instructions = {
        make_instruction(
            1,
            "producer",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
        make_instruction(
            2,
            "consumer",
            {make_input_port("input", "int")},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
    };
    function.edges = {
        EdgeDescriptor{1, "output", 2, "input"},
    };

    const GraphValidator validator;
    const auto result = validator.validate(function);
    return contains_issue(result, GraphValidationCode::edge_type_mismatch);
}

bool test_actor_function_requires_actor_instruction()
{
    FunctionDescriptor function;
    function.id = "actor-function";
    function.generates_actor = true;
    function.instructions = {
        make_instruction(
            1,
            "plain",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
    };

    const GraphValidator validator;
    const auto result = validator.validate(function);
    return contains_issue(result, GraphValidationCode::function_generates_actor_without_actor_instruction);
}

bool test_cycle_detected()
{
    FunctionDescriptor function;
    function.id = "cycle";
    function.instructions = {
        make_instruction(
            1,
            "a",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
        make_instruction(
            2,
            "b",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
    };
    function.edges = {
        EdgeDescriptor{1, "output", 2, "input"},
        EdgeDescriptor{2, "output", 1, "input"},
    };

    const GraphValidator validator;
    const auto result = validator.validate(function);
    return contains_issue(result, GraphValidationCode::cycle_detected);
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

    ok = run_test("valid graph", test_valid_graph) && ok;
    ok = run_test("duplicate instruction id", test_duplicate_instruction_id) && ok;
    ok = run_test("duplicate function input port", test_duplicate_function_input_port) && ok;
    ok = run_test("missing target port", test_missing_target_port) && ok;
    ok = run_test("missing else port", test_missing_else_port) && ok;
    ok = run_test("wrong instruction input port direction", test_wrong_instruction_input_port_direction) && ok;
    ok = run_test("type mismatch", test_type_mismatch) && ok;
    ok = run_test("actor function requires actor instruction", test_actor_function_requires_actor_instruction) && ok;
    ok = run_test("cycle detected", test_cycle_detected) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
