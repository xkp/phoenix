#include "phoenix/partial_run_instruction_index.hpp"
#include "phoenix/partial_run_scope_index.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

phoenix::PortDescriptor make_output_port(const char* id, const char* type)
{
    return phoenix::PortDescriptor{id, type, phoenix::PortDirection::output};
}

phoenix::InstructionDescriptor make_instruction(
    phoenix::NodeId id,
    const char* kind)
{
    phoenix::InstructionDescriptor instruction;
    instruction.id = id;
    instruction.kind = kind;
    instruction.has_else_port = false;
    instruction.output_ports = {make_output_port("output", "geometry")};
    return instruction;
}

void register_noop_handler(phoenix::InstructionRegistry& registry, const char* kind)
{
    registry.register_handler(
        kind,
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("noop")}},
                std::nullopt,
            };
        });
}

struct TestProgram {
    phoenix::FunctionDescriptor parent;
    phoenix::FunctionDescriptor child;
};

TestProgram make_nested_program()
{
    TestProgram program;
    program.child.id = "child";
    program.child.generates_actor = true;
    program.child.instructions = {
        make_instruction(7, "child_step"),
    };

    auto call_child = make_instruction(2, "call_child");
    call_child.called_function_id = program.child.id;
    program.parent.id = "parent";
    program.parent.instructions = {
        call_child,
        make_instruction(3, "parent_step"),
    };

    return program;
}

phoenix::FunctionExecutionRequest make_request(
    const phoenix::FunctionDescriptor& function,
    phoenix::ExecutionTraceLevel trace_level,
    phoenix::FunctionExecutionScopeTraceSink* scope_sink,
    phoenix::FunctionExecutionInstructionTraceSink* instruction_sink)
{
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 77;
    request.trace_level = trace_level;
    request.scope_trace_sink = scope_sink;
    request.instruction_trace_sink = instruction_sink;
    return request;
}

bool test_trace_level_none_records_nothing()
{
    auto program = make_nested_program();

    phoenix::InstructionRegistry registry;
    register_noop_handler(registry, "child_step");
    register_noop_handler(registry, "parent_step");

    phoenix::FunctionLibrary functions;
    functions.register_function(program.parent);
    functions.register_function(program.child);

    phoenix::PartialRerunScopeIndex scope_index;
    phoenix::PartialRerunInstructionIndex instruction_index;
    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(make_request(
        program.parent,
        phoenix::ExecutionTraceLevel::none,
        &scope_index,
        &instruction_index));

    return result.status == phoenix::FunctionExecutionStatus::completed
        && scope_index.scopes().empty()
        && instruction_index.instructions().empty();
}

bool test_trace_level_scope_records_only_scopes()
{
    auto program = make_nested_program();

    phoenix::InstructionRegistry registry;
    register_noop_handler(registry, "child_step");
    register_noop_handler(registry, "parent_step");

    phoenix::FunctionLibrary functions;
    functions.register_function(program.parent);
    functions.register_function(program.child);

    phoenix::PartialRerunScopeIndex scope_index;
    phoenix::PartialRerunInstructionIndex instruction_index;
    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(make_request(
        program.parent,
        phoenix::ExecutionTraceLevel::scope,
        &scope_index,
        &instruction_index));

    return result.status == phoenix::FunctionExecutionStatus::completed
        && scope_index.scopes().size() == 2
        && instruction_index.instructions().empty();
}

bool test_trace_level_instruction_records_compact_instruction_records()
{
    auto program = make_nested_program();

    phoenix::InstructionRegistry registry;
    register_noop_handler(registry, "child_step");
    register_noop_handler(registry, "parent_step");

    phoenix::FunctionLibrary functions;
    functions.register_function(program.parent);
    functions.register_function(program.child);

    phoenix::PartialRerunScopeIndex scope_index;
    phoenix::PartialRerunInstructionIndex instruction_index;
    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(make_request(
        program.parent,
        phoenix::ExecutionTraceLevel::instruction,
        &scope_index,
        &instruction_index));

    const auto child_hits = instruction_index.find_by_function_and_node("child", 7);
    const auto parent_hits = instruction_index.find_by_call_path_and_node({"root"}, 3);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && scope_index.scopes().size() == 2
        && instruction_index.instructions().size() == 3
        && child_hits.size() == 1
        && parent_hits.size() == 1
        && instruction_index.instructions()[child_hits.front()].call_path
            == phoenix::FunctionCallPath({"root", "2:child"})
        && instruction_index.instructions()[child_hits.front()].actor_id.has_value()
        && *instruction_index.instructions()[child_hits.front()].actor_id
            == "actor:root:2:child"
        && instruction_index.instructions()[parent_hits.front()].instruction_kind == "parent_step";
}

bool test_instruction_index_can_return_multiple_calls_for_same_function_node()
{
    phoenix::FunctionDescriptor child;
    child.id = "child";
    child.instructions = {
        make_instruction(7, "child_step"),
    };

    auto first_call = make_instruction(2, "call_first_child");
    first_call.called_function_id = child.id;
    auto second_call = make_instruction(4, "call_second_child");
    second_call.called_function_id = child.id;

    phoenix::FunctionDescriptor parent;
    parent.id = "parent";
    parent.instructions = {first_call, second_call};

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    phoenix::InstructionRegistry registry;
    register_noop_handler(registry, "child_step");

    phoenix::PartialRerunInstructionIndex instruction_index;
    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(make_request(
        parent,
        phoenix::ExecutionTraceLevel::instruction,
        nullptr,
        &instruction_index));

    const auto child_hits = instruction_index.find_by_function_and_node("child", 7);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && child_hits.size() == 2
        && instruction_index.instructions()[child_hits[0]].call_path
            == phoenix::FunctionCallPath({"root", "2:child"})
        && instruction_index.instructions()[child_hits[1]].call_path
            == phoenix::FunctionCallPath({"root", "4:child"});
}

bool test_trace_level_strings_are_stable()
{
    return phoenix::to_string(phoenix::ExecutionTraceLevel::none) == "none"
        && phoenix::to_string(phoenix::ExecutionTraceLevel::scope) == "scope"
        && phoenix::to_string(phoenix::ExecutionTraceLevel::instruction) == "instruction"
        && phoenix::to_string(phoenix::ExecutionTraceLevel::item) == "item"
        && phoenix::to_string(phoenix::ExecutionTraceLevel::value) == "value";
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

    ok = run_test("trace level none records nothing", test_trace_level_none_records_nothing) && ok;
    ok = run_test("trace level scope records only scopes", test_trace_level_scope_records_only_scopes) && ok;
    ok = run_test("trace level instruction records compact instruction records", test_trace_level_instruction_records_compact_instruction_records) && ok;
    ok = run_test("instruction index can return multiple calls for same function node", test_instruction_index_can_return_multiple_calls_for_same_function_node) && ok;
    ok = run_test("trace level strings are stable", test_trace_level_strings_are_stable) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
