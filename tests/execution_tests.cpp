#include "phoenix/execution.hpp"
#include "phoenix/graph.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <utility>

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

FunctionDescriptor make_linear_function()
{
    FunctionDescriptor function;
    function.id = "linear";
    function.input_ports = {make_input_port("input", "geometry")};
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            1,
            "tag_source",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            2,
            "tag_consumer",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {
        EdgeDescriptor{1, "output", 2, "input"},
        EdgeDescriptor{2, "output", 99, "result"},
    };
    function.output_node_id = 99;
    return function;
}

const phoenix::RuntimeValue* find_input(
    const phoenix::InstructionExecutionFrame& frame,
    const char* port)
{
    for (const auto& input : frame.inputs.promised_inputs) {
        if (input.port == port) {
            return &input.value;
        }
    }

    return nullptr;
}

bool output_has_geometry_label(
    const phoenix::FunctionExecutionResult& result,
    const char* port,
    const char* label)
{
    for (const auto& output : result.outputs) {
        if (output.port != port) {
            continue;
        }

        const auto* geometry = output.value.as_geometry();
        return geometry != nullptr && geometry->debug_label == label;
    }

    return false;
}

bool node_state_is(
    const phoenix::FunctionExecutionResult& result,
    phoenix::NodeId node_id,
    phoenix::InstructionState expected)
{
    for (const auto& state : result.node_states) {
        if (state.node_id == node_id) {
            return state.state == expected;
        }
    }

    return false;
}

bool frame_has_missing_input(
    const phoenix::InstructionExecutionFrame& frame,
    const char* port)
{
    const auto* input = find_input(frame, port);
    return input != nullptr && input->is_missing();
}

bool frame_has_defaulted_input(
    const phoenix::InstructionExecutionFrame& frame,
    const char* port,
    const char* source_type)
{
    const auto* input = find_input(frame, port);
    if (input == nullptr || !input->is_defaulted()) {
        return false;
    }

    const auto* default_value = input->as_default();
    return default_value != nullptr && default_value->source_type == source_type;
}

bool test_linear_execution_propagates_outputs()
{
    const auto function = make_linear_function();

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "tag_source",
        [](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            if (input == nullptr || input->as_geometry() == nullptr) {
                return phoenix::InstructionResult{frame.inputs.node_id, {}, "source input missing"};
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("source")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "tag_consumer",
        [](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            if (input == nullptr || input->as_geometry() == nullptr) {
                return phoenix::InstructionResult{frame.inputs.node_id, {}, "consumer input missing"};
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("consumer")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry("initial")}};
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 7;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && !result.failure_message.has_value()
        && output_has_geometry_label(result, "result", "consumer")
        && node_state_is(result, 1, phoenix::InstructionState::completed)
        && node_state_is(result, 2, phoenix::InstructionState::completed)
        && node_state_is(result, 99, phoenix::InstructionState::idle);
}

bool test_missing_output_leaves_function_output_empty()
{
    auto function = make_linear_function();

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "tag_source",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::missing()}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "tag_consumer",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("should_not_run")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry("initial")}};

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && !result.failure_message.has_value()
        && result.outputs.size() == 1
        && result.outputs.front().port == "result"
        && result.outputs.front().value.is_empty()
        && node_state_is(result, 2, phoenix::InstructionState::idle);
}

bool test_equilibrium_force_runs_smallest_independent_pending_node()
{
    FunctionDescriptor function;
    function.id = "equilibrium";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            10,
            "source_a",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            11,
            "source_b",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            1,
            "first",
            {make_input_port("a", "geometry"), make_input_port("b", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            2,
            "second",
            {make_input_port("a", "geometry"), make_input_port("b", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {
        EdgeDescriptor{10, "output", 1, "a"},
        EdgeDescriptor{10, "output", 2, "a"},
        EdgeDescriptor{11, "output", 1, "b"},
        EdgeDescriptor{11, "output", 2, "b"},
        EdgeDescriptor{1, "output", 99, "result"},
    };
    function.output_node_id = 99;

    phoenix::InstructionRegistry registry;
    std::vector<phoenix::NodeId> run_order;
    registry.register_handler(
        "source_a",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("a")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "source_b",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::missing()}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "first",
        [&run_order](const phoenix::InstructionExecutionFrame& frame) {
            run_order.push_back(frame.inputs.node_id);
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("forced-first")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "second",
        [&run_order](const phoenix::InstructionExecutionFrame& frame) {
            run_order.push_back(frame.inputs.node_id);
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("forced-second")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && !run_order.empty()
        && run_order.front() == 1
        && output_has_geometry_label(result, "result", "forced-first");
}

bool test_forced_run_includes_missing_promised_input()
{
    FunctionDescriptor function;
    function.id = "forced-missing";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            10,
            "source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            11,
            "missing_source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            20,
            "needs_two",
            {make_input_port("a", "geometry"), make_input_port("b", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {
        EdgeDescriptor{10, "output", 20, "a"},
        EdgeDescriptor{11, "output", 20, "b"},
        EdgeDescriptor{20, "output", 99, "result"},
    };
    function.output_node_id = 99;

    bool saw_missing_b = false;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "source",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("a")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "missing_source",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::missing()}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "needs_two",
        [&saw_missing_b](const phoenix::InstructionExecutionFrame& frame) {
            saw_missing_b = frame_has_missing_input(frame, "b");
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("forced")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && saw_missing_b
        && output_has_geometry_label(result, "result", "forced");
}

bool test_forced_run_injects_default_input()
{
    FunctionDescriptor function;
    function.id = "forced-default";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            10,
            "source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            20,
            "needs_default",
            {make_input_port("input", "geometry"), make_input_port("count", "int")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {
        EdgeDescriptor{10, "output", 20, "input"},
        EdgeDescriptor{20, "output", 99, "result"},
    };
    function.output_node_id = 99;

    bool saw_default_count = false;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "source",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("mesh")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "needs_default",
        [&saw_default_count](const phoenix::InstructionExecutionFrame& frame) {
            saw_default_count = frame_has_defaulted_input(frame, "count", "int");
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("defaulted")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.input_defaults[20] = {
        phoenix::PortValue{"count", phoenix::RuntimeValue::defaulted("int")},
    };

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && saw_default_count
        && output_has_geometry_label(result, "result", "defaulted");
}

bool test_function_without_outputs_does_not_need_output_node()
{
    FunctionDescriptor function;
    function.id = "no-return";
    function.instructions = {
        make_instruction(
            1,
            "side_effect_free_sink",
            {},
            {make_output_port("output", "geometry")}),
    };

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "side_effect_free_sink",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.outputs.empty()
        && node_state_is(result, 1, phoenix::InstructionState::completed);
}

bool test_returning_function_requires_output_node()
{
    FunctionDescriptor function;
    function.id = "missing-output-node";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            1,
            "producer",
            {},
            {make_output_port("output", "geometry")}),
    };

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "producer",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::invalid_graph
        && result.failure_message.has_value();
}

bool test_missing_handler_reports_status()
{
    FunctionDescriptor function;
    function.id = "missing-handler";
    function.instructions = {
        make_instruction(
            1,
            "unregistered",
            {},
            {make_output_port("output", "geometry")}),
    };

    phoenix::InstructionRegistry registry;

    phoenix::FunctionExecutionRequest request;
    request.function = &function;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::missing_handler
        && result.failure_message.has_value();
}

bool test_nested_function_call_pushes_call_frame()
{
    FunctionDescriptor child;
    child.id = "child";
    child.output_ports = {make_output_port("result", "geometry")};
    child.instructions = {
        make_instruction(
            7,
            "child_producer",
            {},
            {make_output_port("result", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    child.edges = {EdgeDescriptor{7, "result", 99, "result"}};
    child.output_node_id = 99;

    FunctionDescriptor parent;
    parent.id = "parent";
    parent.output_ports = {make_output_port("result", "geometry")};
    auto call_child = make_instruction(
        1,
        "call",
        {},
        {make_output_port("result", "geometry")});
    call_child.called_function_id = "child";
    parent.instructions = {
        call_child,
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    parent.edges = {EdgeDescriptor{1, "result", 99, "result"}};
    parent.output_node_id = 99;

    bool saw_child_frame = false;

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "child_producer",
        [&saw_child_frame](const phoenix::InstructionExecutionFrame& frame) {
            const auto* current = frame.call_stack.current();
            saw_child_frame = frame.call_stack.size() == 2
                && current != nullptr
                && current->function_id == "child"
                && current->caller_node_id.has_value()
                && *current->caller_node_id == 1
                && !current->call_path.empty()
                && current->call_path.back() == "1:child";

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"result", phoenix::RuntimeValue::geometry("from-child")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && saw_child_frame
        && output_has_geometry_label(result, "result", "from-child");
}

bool test_function_call_requires_function_library()
{
    FunctionDescriptor parent;
    parent.id = "parent";
    auto call_child = make_instruction(
        1,
        "call",
        {},
        {make_output_port("result", "geometry")});
    call_child.called_function_id = "child";
    parent.instructions = {call_child};

    phoenix::InstructionRegistry registry;
    phoenix::FunctionExecutionRequest request;
    request.function = &parent;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::invalid_request
        && result.failure_message.has_value();
}

bool test_function_call_reports_missing_callee()
{
    FunctionDescriptor parent;
    parent.id = "parent";
    auto call_child = make_instruction(
        1,
        "call",
        {},
        {make_output_port("result", "geometry")});
    call_child.called_function_id = "missing_child";
    parent.instructions = {call_child};

    phoenix::InstructionRegistry registry;
    phoenix::FunctionLibrary functions;
    functions.register_function(parent);

    phoenix::FunctionExecutionRequest request;
    request.function = &parent;

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::invalid_graph
        && result.failure_message.has_value();
}

bool test_effective_seed_is_deterministic()
{
    FunctionDescriptor function;
    function.id = "seeded";
    function.output_ports = {make_output_port("result", "int")};
    auto instruction = make_instruction(
        7,
        "seed_echo",
        {},
        {make_output_port("output", "int")});
    instruction.local_seed = 5;
    function.instructions = {
        instruction,
        make_instruction(99, "output", {make_input_port("result", "int")}, {}),
    };
    function.edges = {EdgeDescriptor{7, "output", 99, "result"}};
    function.output_node_id = 99;

    std::optional<phoenix::SeedValue> first_seed;
    std::optional<phoenix::SeedValue> second_seed;

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "seed_echo",
        [&first_seed, &second_seed](const phoenix::InstructionExecutionFrame& frame) {
            if (!first_seed.has_value()) {
                first_seed = *frame.effective_seed;
            } else {
                second_seed = *frame.effective_seed;
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::literal(
                    phoenix::LiteralValue{static_cast<std::int64_t>(*frame.effective_seed)})}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root", "seeded"};
    request.context.global_seed = 42;

    const phoenix::FunctionExecutor executor(registry);
    const auto first = executor.run(request);
    const auto second = executor.run(request);

    return first.status == phoenix::FunctionExecutionStatus::completed
        && second.status == phoenix::FunctionExecutionStatus::completed
        && !first.failure_message.has_value()
        && !second.failure_message.has_value()
        && first_seed.has_value()
        && second_seed.has_value()
        && first_seed == second_seed;
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

    ok = run_test("linear execution propagates outputs", test_linear_execution_propagates_outputs) && ok;
    ok = run_test("missing output leaves function output empty", test_missing_output_leaves_function_output_empty) && ok;
    ok = run_test("equilibrium force-runs smallest independent pending node", test_equilibrium_force_runs_smallest_independent_pending_node) && ok;
    ok = run_test("forced run includes missing promised input", test_forced_run_includes_missing_promised_input) && ok;
    ok = run_test("forced run injects default input", test_forced_run_injects_default_input) && ok;
    ok = run_test("function without outputs does not need output node", test_function_without_outputs_does_not_need_output_node) && ok;
    ok = run_test("returning function requires output node", test_returning_function_requires_output_node) && ok;
    ok = run_test("missing handler reports status", test_missing_handler_reports_status) && ok;
    ok = run_test("nested function call pushes call frame", test_nested_function_call_pushes_call_frame) && ok;
    ok = run_test("function call requires function library", test_function_call_requires_function_library) && ok;
    ok = run_test("function call reports missing callee", test_function_call_reports_missing_callee) && ok;
    ok = run_test("effective seed is deterministic", test_effective_seed_is_deterministic) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
