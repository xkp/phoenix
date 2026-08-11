#include "phoenix/execution.hpp"
#include "phoenix/graph.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <thread>
#include <utility>

namespace {

using phoenix::EdgeDescriptor;
using phoenix::FunctionDescriptor;
using phoenix::InstructionDescriptor;
using phoenix::PortDescriptor;
using phoenix::PortDirection;

class InstructionOrderTrace final : public phoenix::FunctionExecutionInstructionTraceSink {
public:
    void record_instruction(phoenix::FunctionExecutionInstructionRecord instruction) override
    {
        order.push_back(instruction.node_id);
    }

    std::vector<phoenix::NodeId> order;
};

class PublicationTrace final : public phoenix::FunctionExecutionPublicationTraceSink {
public:
    void record_publication(phoenix::FunctionExecutionPublicationRecord publication) override
    {
        records.push_back(std::move(publication));
    }

    std::vector<phoenix::FunctionExecutionPublicationRecord> records;
};

class DiagnosticsTrace final : public phoenix::FunctionExecutionDiagnosticsSink {
public:
    void record_diagnostics(phoenix::FunctionExecutionDiagnosticsRecord diagnostics) override
    {
        records.push_back(std::move(diagnostics));
    }

    std::vector<phoenix::FunctionExecutionDiagnosticsRecord> records;
};

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

bool geometry_values_equal(const phoenix::GeometryValue& left, const phoenix::GeometryValue& right)
{
    return left.debug_label == right.debug_label
        && left.accumulation_actor_id == right.accumulation_actor_id;
}

bool runtime_values_equal(const phoenix::RuntimeValue& left, const phoenix::RuntimeValue& right)
{
    if (left.presence != right.presence) {
        return false;
    }

    if (const auto* left_geometry = left.as_geometry()) {
        const auto* right_geometry = right.as_geometry();
        return right_geometry != nullptr && geometry_values_equal(*left_geometry, *right_geometry);
    }

    if (const auto* left_collection = left.as_geometry_collection()) {
        const auto* right_collection = right.as_geometry_collection();
        if (right_collection == nullptr
            || left_collection->contributions.size() != right_collection->contributions.size()) {
            return false;
        }

        for (std::size_t i = 0; i < left_collection->contributions.size(); ++i) {
            if (!geometry_values_equal(
                    left_collection->contributions[i],
                    right_collection->contributions[i])) {
                return false;
            }
        }

        return true;
    }

    if (const auto* left_literal = left.as_literal()) {
        const auto* right_literal = right.as_literal();
        return right_literal != nullptr && *left_literal == *right_literal;
    }

    if (const auto* left_default = left.as_default()) {
        const auto* right_default = right.as_default();
        return right_default != nullptr && left_default->source_type == right_default->source_type;
    }

    return right.is_missing() || right.is_empty();
}

bool port_values_equal(
    const std::vector<phoenix::PortValue>& left,
    const std::vector<phoenix::PortValue>& right)
{
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i].port != right[i].port
            || !runtime_values_equal(left[i].value, right[i].value)) {
            return false;
        }
    }

    return true;
}

bool actors_observably_equal(const phoenix::ActorNode& left, const phoenix::ActorNode& right)
{
    if (left.id != right.id
        || left.name != right.name
        || left.geometry.has_value() != right.geometry.has_value()
        || left.children.size() != right.children.size()
        || left.prototype.has_value() != right.prototype.has_value()) {
        return false;
    }

    if (left.geometry.has_value() && !geometry_values_equal(*left.geometry, *right.geometry)) {
        return false;
    }

    if (left.prototype.has_value()
        && left.prototype->prototype_id != right.prototype->prototype_id) {
        return false;
    }

    for (std::size_t i = 0; i < left.children.size(); ++i) {
        if (!actors_observably_equal(left.children[i], right.children[i])) {
            return false;
        }
    }

    return true;
}

bool execution_results_observably_equal(
    const phoenix::FunctionExecutionResult& left,
    const phoenix::FunctionExecutionResult& right)
{
    if (left.status != right.status
        || left.failure_message != right.failure_message
        || !port_values_equal(left.outputs, right.outputs)
        || left.actor.has_value() != right.actor.has_value()) {
        return false;
    }

    return !left.actor.has_value() || actors_observably_equal(*left.actor, *right.actor);
}

bool output_has_geometry_collection_labels(
    const phoenix::FunctionExecutionResult& result,
    const char* port,
    const std::vector<const char*>& labels)
{
    for (const auto& output : result.outputs) {
        if (output.port != port) {
            continue;
        }

        const auto* collection = output.value.as_geometry_collection();
        if (collection == nullptr || collection->contributions.size() != labels.size()) {
            return false;
        }

        for (std::size_t i = 0; i < labels.size(); ++i) {
            if (collection->contributions[i].debug_label != labels[i]) {
                return false;
            }
        }

        return true;
    }

    return false;
}

bool output_geometry_has_owner(
    const phoenix::FunctionExecutionResult& result,
    const char* port,
    const char* owner)
{
    for (const auto& output : result.outputs) {
        if (output.port != port) {
            continue;
        }

        const auto* geometry = output.value.as_geometry();
        return geometry != nullptr
            && geometry->accumulation_actor_id.has_value()
            && *geometry->accumulation_actor_id == owner;
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

bool test_ready_frontier_admits_downstream_after_each_completion()
{
    FunctionDescriptor function;
    function.id = "frontier";
    function.instructions = {
        make_instruction(
            10,
            "frontier_source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            20,
            "frontier_downstream",
            {make_input_port("input", "geometry")},
            {}),
        make_instruction(
            50,
            "frontier_independent",
            {},
            {}),
    };
    function.edges = {EdgeDescriptor{10, "output", 20, "input"}};

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "frontier_source",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("frontier")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "frontier_downstream",
        [](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            if (input == nullptr || input->as_geometry() == nullptr) {
                return phoenix::InstructionResult{
                    frame.inputs.node_id,
                    {},
                    "frontier downstream input missing",
                };
            }

            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });
    registry.register_handler(
        "frontier_independent",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });

    InstructionOrderTrace trace;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.trace_level = phoenix::ExecutionTraceLevel::instruction;
    request.instruction_trace_sink = &trace;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && trace.order == std::vector<phoenix::NodeId>{10, 20, 50};
}

bool test_downstream_waits_for_all_geometry_contributions()
{
    FunctionDescriptor function;
    function.id = "frontier-contributions";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            10,
            "frontier_first_contribution",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            20,
            "frontier_join_contributions",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            50,
            "frontier_second_contribution",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {
        EdgeDescriptor{10, "output", 20, "input"},
        EdgeDescriptor{50, "output", 20, "input"},
        EdgeDescriptor{20, "output", 99, "result"},
    };
    function.output_node_id = 99;

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "frontier_first_contribution",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("first")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "frontier_second_contribution",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("second")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "frontier_join_contributions",
        [](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            const auto* collection = input == nullptr ? nullptr : input->as_geometry_collection();
            if (collection == nullptr
                || collection->contributions.size() != 2
                || collection->contributions[0].debug_label != "first"
                || collection->contributions[1].debug_label != "second") {
                return phoenix::InstructionResult{
                    frame.inputs.node_id,
                    {},
                    "frontier joined before all geometry contributions arrived",
                };
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("joined")}},
                std::nullopt,
            };
        });

    InstructionOrderTrace trace;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.trace_level = phoenix::ExecutionTraceLevel::instruction;
    request.instruction_trace_sink = &trace;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && trace.order == std::vector<phoenix::NodeId>{10, 50, 20}
        && output_has_geometry_label(result, "result", "joined");
}

bool test_worker_count_runs_independent_handlers_concurrently()
{
    FunctionDescriptor function;
    function.id = "threaded-frontier";
    function.instructions = {
        make_instruction(1, "threaded_slow_a", {}, {}),
        make_instruction(2, "threaded_slow_b", {}, {}),
    };

    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    const auto record_overlap = [&active, &max_active]() {
        const int now_active = active.fetch_add(1) + 1;
        int observed = max_active.load();
        while (observed < now_active
            && !max_active.compare_exchange_weak(observed, now_active)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        active.fetch_sub(1);
    };

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "threaded_slow_a",
        [&record_overlap](const phoenix::InstructionExecutionFrame& frame) {
            record_overlap();
            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });
    registry.register_handler(
        "threaded_slow_b",
        [&record_overlap](const phoenix::InstructionExecutionFrame& frame) {
            record_overlap();
            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });

    PublicationTrace publication_trace;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 2;
    request.trace_level = phoenix::ExecutionTraceLevel::instruction;
    request.publication_trace_sink = &publication_trace;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && max_active.load() == 2
        && publication_trace.records.size() == 2
        && publication_trace.records[0].node_id == 1
        && publication_trace.records[1].node_id == 2;
}

bool test_threaded_regular_handlers_keep_traces_ordered()
{
    FunctionDescriptor function;
    function.id = "threaded-traced-handlers";
    function.instructions = {
        make_instruction(1, "threaded_traced_slow", {}, {}),
        make_instruction(2, "threaded_traced_fast", {}, {}),
    };

    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    const auto record_active = [&active, &max_active](std::chrono::milliseconds delay) {
        const int now_active = active.fetch_add(1) + 1;
        int observed = max_active.load();
        while (observed < now_active
            && !max_active.compare_exchange_weak(observed, now_active)) {
        }
        std::this_thread::sleep_for(delay);
        active.fetch_sub(1);
    };

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "threaded_traced_slow",
        [&record_active](const phoenix::InstructionExecutionFrame& frame) {
            record_active(std::chrono::milliseconds(100));
            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });
    registry.register_handler(
        "threaded_traced_fast",
        [&record_active](const phoenix::InstructionExecutionFrame& frame) {
            record_active(std::chrono::milliseconds(10));
            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });

    InstructionOrderTrace instruction_trace;
    PublicationTrace publication_trace;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 2;
    request.trace_level = phoenix::ExecutionTraceLevel::instruction;
    request.instruction_trace_sink = &instruction_trace;
    request.publication_trace_sink = &publication_trace;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && max_active.load() == 2
        && instruction_trace.order == std::vector<phoenix::NodeId>{1, 2}
        && publication_trace.records.size() == 2
        && publication_trace.records[0].node_id == 1
        && publication_trace.records[1].node_id == 2;
}

bool test_worker_count_preserves_observable_results()
{
    auto function = make_linear_function();
    function.instructions.insert(
        function.instructions.begin(),
        make_instruction(3, "parallel_extra", {}, {make_output_port("output", "geometry")}));

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "tag_source",
        [](const phoenix::InstructionExecutionFrame& frame) {
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
                return phoenix::InstructionResult{
                    frame.inputs.node_id,
                    {},
                    "consumer input missing",
                };
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("consumer")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "parallel_extra",
        [](const phoenix::InstructionExecutionFrame& frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("extra")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest serial_request;
    serial_request.function = &function;
    serial_request.inputs = {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry("initial")}};
    serial_request.context.function_id = function.id;
    serial_request.context.call_path = {"root"};

    auto threaded_request = serial_request;
    threaded_request.options.worker_count = 2;

    const phoenix::FunctionExecutor executor(registry);
    const auto serial_result = executor.run(serial_request);
    const auto threaded_result = executor.run(threaded_request);

    return execution_results_observably_equal(serial_result, threaded_result);
}

bool test_threaded_publication_order_ignores_completion_order()
{
    FunctionDescriptor function;
    function.id = "threaded-publication-order";
    function.instructions = {
        make_instruction(1, "threaded_slower", {}, {}),
        make_instruction(2, "threaded_faster", {}, {}),
    };

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "threaded_slower",
        [](const phoenix::InstructionExecutionFrame& frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });
    registry.register_handler(
        "threaded_faster",
        [](const phoenix::InstructionExecutionFrame& frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });

    PublicationTrace publication_trace;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 2;
    request.trace_level = phoenix::ExecutionTraceLevel::instruction;
    request.publication_trace_sink = &publication_trace;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && publication_trace.records.size() == 2
        && publication_trace.records[0].node_id == 1
        && publication_trace.records[1].node_id == 2;
}

bool test_threaded_handler_failure_uses_central_publication()
{
    FunctionDescriptor function;
    function.id = "threaded-failure";
    function.output_ports = {
        make_output_port("fallback", "geometry"),
    };
    auto failing = make_instruction(
        1,
        "threaded_fail_with_else",
        {},
        {make_output_port("else", "geometry")});
    failing.failure_is_critical = true;
    function.instructions = {
        failing,
        make_instruction(
            2,
            "threaded_failure_fallback",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("fallback", "geometry")},
            {}),
    };
    function.edges = {
        EdgeDescriptor{1, "else", 2, "input"},
        EdgeDescriptor{2, "output", 99, "fallback"},
    };
    function.output_node_id = 99;

    bool fallback_saw_failure = false;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "threaded_fail_with_else",
        [](const phoenix::InstructionExecutionFrame& frame) {
            phoenix::InstructionResult result;
            result.node_id = frame.inputs.node_id;
            result.failures = {
                phoenix::InstructionFailure{
                    frame.inputs.node_id,
                    std::uint64_t{7},
                    "threaded item failed",
                    {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry("failed")}},
                    frame.call_stack,
                },
            };
            return result;
        });
    registry.register_handler(
        "threaded_failure_fallback",
        [&fallback_saw_failure](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            fallback_saw_failure = geometry != nullptr && geometry->debug_label == "failed";
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("fallback")}},
                std::nullopt,
            };
        });

    PublicationTrace publication_trace;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 2;
    request.trace_level = phoenix::ExecutionTraceLevel::instruction;
    request.publication_trace_sink = &publication_trace;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.failures.size() == 1
        && fallback_saw_failure
        && output_has_geometry_label(result, "fallback", "fallback")
        && publication_trace.records.size() == 2
        && publication_trace.records[0].node_id == 1
        && publication_trace.records[0].failure_count == 1;
}

bool test_force_run_remains_serial_with_worker_count()
{
    FunctionDescriptor function;
    function.id = "threaded-force-fallback";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            1,
            "threaded_force_source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            4,
            "threaded_force_missing_source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            2,
            "threaded_force_first",
            {make_input_port("left", "geometry"), make_input_port("right", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            3,
            "threaded_force_second",
            {make_input_port("left", "geometry"), make_input_port("right", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {
        EdgeDescriptor{1, "output", 2, "left"},
        EdgeDescriptor{1, "output", 3, "left"},
        EdgeDescriptor{4, "output", 2, "right"},
        EdgeDescriptor{4, "output", 3, "right"},
        EdgeDescriptor{2, "output", 99, "result"},
        EdgeDescriptor{3, "output", 99, "result"},
    };
    function.output_node_id = 99;

    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    const auto record_active = [&active, &max_active]() {
        const int now_active = active.fetch_add(1) + 1;
        int observed = max_active.load();
        while (observed < now_active
            && !max_active.compare_exchange_weak(observed, now_active)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        active.fetch_sub(1);
    };

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "threaded_force_source",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("source")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "threaded_force_missing_source",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::missing()}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "threaded_force_first",
        [&record_active](const phoenix::InstructionExecutionFrame& frame) {
            record_active();
            if (find_input(frame, "right") == nullptr) {
                return phoenix::InstructionResult{
                    frame.inputs.node_id,
                    {},
                    "forced input was not included",
                };
            }
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("first")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "threaded_force_second",
        [&record_active](const phoenix::InstructionExecutionFrame& frame) {
            record_active();
            if (find_input(frame, "right") == nullptr) {
                return phoenix::InstructionResult{
                    frame.inputs.node_id,
                    {},
                    "forced input was not included",
                };
            }
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("second")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 2;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && max_active.load() == 1
        && output_has_geometry_collection_labels(result, "result", {"first", "second"});
}

bool test_cache_request_keeps_regular_handlers_serial()
{
    FunctionDescriptor function;
    function.id = "threaded-cache-fallback";
    function.instructions = {
        make_instruction(1, "cache_fallback_slow_a", {}, {}),
        make_instruction(2, "cache_fallback_slow_b", {}, {}),
    };

    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    const auto record_active = [&active, &max_active]() {
        const int now_active = active.fetch_add(1) + 1;
        int observed = max_active.load();
        while (observed < now_active
            && !max_active.compare_exchange_weak(observed, now_active)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        active.fetch_sub(1);
    };

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "cache_fallback_slow_a",
        [&record_active](const phoenix::InstructionExecutionFrame& frame) {
            record_active();
            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });
    registry.register_handler(
        "cache_fallback_slow_b",
        [&record_active](const phoenix::InstructionExecutionFrame& frame) {
            record_active();
            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });

    phoenix::MemoryCacheStore cache_store;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 2;
    request.cache_store = &cache_store;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && max_active.load() == 1;
}

bool test_diagnostics_records_threaded_instruction_metrics_in_publication_order()
{
    FunctionDescriptor function;
    function.id = "diagnosed-threaded";
    function.instructions = {
        make_instruction(1, "diagnosed_slow", {}, {make_output_port("output", "geometry")}),
        make_instruction(2, "diagnosed_fast", {}, {make_output_port("output", "geometry")}),
    };

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "diagnosed_slow",
        [](const phoenix::InstructionExecutionFrame& frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("slow")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "diagnosed_fast",
        [](const phoenix::InstructionExecutionFrame& frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("fast")}},
                std::nullopt,
            };
        });

    DiagnosticsTrace diagnostics;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 2;
    request.diagnostics_sink = &diagnostics;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && diagnostics.records.size() == 2
        && diagnostics.records[0].node_id == 1
        && diagnostics.records[1].node_id == 2
        && diagnostics.records[0].execution_mode == phoenix::FunctionExecutionMode::worker
        && diagnostics.records[1].execution_mode == phoenix::FunctionExecutionMode::worker
        && diagnostics.records[0].requested_worker_count == 2
        && diagnostics.records[0].produced_output_count == 1
        && diagnostics.records[1].produced_output_count == 1
        && diagnostics.records[0].elapsed_microseconds > 0
        && diagnostics.records[1].elapsed_microseconds > 0;
}

bool test_diagnostics_records_cache_hits_and_serial_mode()
{
    FunctionDescriptor function;
    function.id = "diagnosed-cache-hit";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(1, "diagnosed_cached_source", {}, {make_output_port("output", "geometry")}),
        make_instruction(
            2,
            "diagnosed_downstream_after_cache",
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

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 44;

    const auto identity = phoenix::CacheIdentityBuilder{}.identity(phoenix::CacheIdentityInput{
        &function,
        request.context.call_path,
        request.inputs,
        request.context.global_seed,
    });
    const auto effective_seed = phoenix::SeedDeriver{}.derive(phoenix::SeedDerivationInput{
        request.context.global_seed,
        request.context.call_path,
        1,
        std::nullopt,
    });

    phoenix::MemoryCacheStore cache_store;
    phoenix::InstructionCacheEntry entry;
    entry.key = phoenix::CacheKeyBuilder{}.instruction_outputs(
        phoenix::InstructionCacheKeyInput{identity, 1, effective_seed});
    entry.outputs = {
        phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("from-cache")},
    };
    cache_store.put_instruction(entry);

    int source_run_count = 0;
    bool downstream_saw_cached_input = false;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "diagnosed_cached_source",
        [&source_run_count](const phoenix::InstructionExecutionFrame& frame) {
            ++source_run_count;
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("uncached")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "diagnosed_downstream_after_cache",
        [&downstream_saw_cached_input](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            downstream_saw_cached_input =
                geometry != nullptr && geometry->debug_label == "from-cache";
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("downstream")}},
                std::nullopt,
            };
        });

    DiagnosticsTrace diagnostics;
    request.cache_store = &cache_store;
    request.options.worker_count = 2;
    request.diagnostics_sink = &diagnostics;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && source_run_count == 0
        && downstream_saw_cached_input
        && output_has_geometry_label(result, "result", "downstream")
        && diagnostics.records.size() == 2
        && diagnostics.records[0].node_id == 1
        && diagnostics.records[0].instruction_cache_hit
        && diagnostics.records[0].execution_mode == phoenix::FunctionExecutionMode::serial
        && diagnostics.records[0].produced_output_count == 1
        && diagnostics.records[1].node_id == 2
        && !diagnostics.records[1].instruction_cache_hit;
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

    PublicationTrace publication_trace;
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

bool test_noncritical_unhandled_failure_is_logged_and_execution_continues()
{
    FunctionDescriptor function;
    function.id = "noncritical-unhandled-failure";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            1,
            "partial_fail",
            {},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {EdgeDescriptor{1, "output", 99, "result"}};
    function.output_node_id = 99;

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "partial_fail",
        [](const phoenix::InstructionExecutionFrame& frame) {
            phoenix::InstructionResult result;
            result.node_id = frame.inputs.node_id;
            result.produced_outputs = {
                phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("successful-item")},
            };
            result.failures = {
                phoenix::InstructionFailure{
                    frame.inputs.node_id,
                    std::uint64_t{4},
                    "failed item",
                    {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry("failed-item")}},
                    frame.call_stack,
                },
            };
            return result;
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.failures.size() == 1
        && !result.failure_message.has_value()
        && output_has_geometry_label(result, "result", "successful-item");
}

bool test_critical_unhandled_failure_fails_function()
{
    FunctionDescriptor function;
    function.id = "critical-unhandled-failure";
    function.output_ports = {make_output_port("result", "geometry")};
    auto critical = make_instruction(
        1,
        "critical_fail",
        {},
        {make_output_port("output", "geometry"), make_output_port("else", "geometry")});
    critical.failure_is_critical = true;
    function.instructions = {
        critical,
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {EdgeDescriptor{1, "output", 99, "result"}};
    function.output_node_id = 99;

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "critical_fail",
        [](const phoenix::InstructionExecutionFrame& frame) {
            phoenix::InstructionResult result;
            result.node_id = frame.inputs.node_id;
            result.produced_outputs = {
                phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("partial")},
            };
            result.failures = {
                phoenix::InstructionFailure{
                    frame.inputs.node_id,
                    std::nullopt,
                    "critical failure",
                    {},
                    frame.call_stack,
                },
            };
            return result;
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::failed
        && result.failures.size() == 1
        && result.failure_message.has_value()
        && output_has_geometry_label(result, "result", "partial");
}

bool test_handled_failure_routes_failed_item_through_else()
{
    FunctionDescriptor function;
    function.id = "handled-failure";
    function.output_ports = {
        make_output_port("success", "geometry"),
        make_output_port("fallback", "geometry"),
    };
    function.instructions = {
        make_instruction(
            1,
            "mixed_mux",
            {},
            {make_output_port("output", "geometry"), make_output_port("else", "geometry")}),
        make_instruction(
            2,
            "fallback",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("success", "geometry"), make_input_port("fallback", "geometry")},
            {}),
    };
    function.edges = {
        EdgeDescriptor{1, "output", 99, "success"},
        EdgeDescriptor{1, "else", 2, "input"},
        EdgeDescriptor{2, "output", 99, "fallback"},
    };
    function.output_node_id = 99;

    bool fallback_saw_failed_item = false;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "mixed_mux",
        [](const phoenix::InstructionExecutionFrame& frame) {
            phoenix::InstructionResult result;
            result.node_id = frame.inputs.node_id;
            result.produced_outputs = {
                phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("success-item")},
            };
            result.failures = {
                phoenix::InstructionFailure{
                    frame.inputs.node_id,
                    std::uint64_t{2},
                    "item failed",
                    {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry("failed-item")}},
                    frame.call_stack,
                },
            };
            return result;
        });
    registry.register_handler(
        "fallback",
        [&fallback_saw_failed_item](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            fallback_saw_failed_item = geometry != nullptr && geometry->debug_label == "failed-item";

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("fallback-item")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.failures.size() == 1
        && fallback_saw_failed_item
        && output_has_geometry_label(result, "success", "success-item")
        && output_has_geometry_label(result, "fallback", "fallback-item");
}

bool test_multiplexed_successes_and_failures_accumulate()
{
    FunctionDescriptor function;
    function.id = "multiplexed-mixed-results";
    function.output_ports = {
        make_output_port("success", "geometry"),
        make_output_port("fallback", "geometry"),
    };
    auto mixed_mux = make_instruction(
        1,
        "mixed_mux_many",
        {},
        {make_output_port("output", "geometry"), make_output_port("else", "geometry")});
    mixed_mux.multiplexes_input = true;
    function.instructions = {
        mixed_mux,
        make_instruction(
            2,
            "fallback_many",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("success", "geometry"), make_input_port("fallback", "geometry")},
            {}),
    };
    function.edges = {
        EdgeDescriptor{1, "output", 99, "success"},
        EdgeDescriptor{1, "else", 2, "input"},
        EdgeDescriptor{2, "output", 99, "fallback"},
    };
    function.output_node_id = 99;

    bool fallback_saw_failed_items = false;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "mixed_mux_many",
        [](const phoenix::InstructionExecutionFrame& frame) {
            phoenix::InstructionResult result;
            result.node_id = frame.inputs.node_id;
            result.produced_outputs = {
                phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("success-1")},
                phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("success-2")},
            };
            result.failures = {
                phoenix::InstructionFailure{
                    frame.inputs.node_id,
                    std::uint64_t{1},
                    "first failed item",
                    {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry("failed-1")}},
                    frame.call_stack,
                },
                phoenix::InstructionFailure{
                    frame.inputs.node_id,
                    std::uint64_t{3},
                    "second failed item",
                    {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry("failed-2")}},
                    frame.call_stack,
                },
            };
            return result;
        });
    registry.register_handler(
        "fallback_many",
        [&fallback_saw_failed_items](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            const auto* collection = input == nullptr ? nullptr : input->as_geometry_collection();
            fallback_saw_failed_items = collection != nullptr
                && collection->contributions.size() == 2
                && collection->contributions[0].debug_label == "failed-1"
                && collection->contributions[1].debug_label == "failed-2";

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("fallback-collection")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.failures.size() == 2
        && result.failures[0].item_key.has_value()
        && *result.failures[0].item_key == 1
        && result.failures[1].item_key.has_value()
        && *result.failures[1].item_key == 3
        && fallback_saw_failed_items
        && output_has_geometry_collection_labels(result, "success", {"success-1", "success-2"})
        && output_has_geometry_label(result, "fallback", "fallback-collection");
}

bool test_nested_function_failure_can_be_handled_by_call_else()
{
    FunctionDescriptor child;
    child.id = "failing-child";
    auto child_failure = make_instruction(
        7,
        "child_failure",
        {},
        {make_output_port("output", "geometry"), make_output_port("else", "geometry")});
    child_failure.failure_is_critical = true;
    child.instructions = {
        child_failure,
    };

    FunctionDescriptor parent;
    parent.id = "parent-handles-child";
    parent.output_ports = {make_output_port("result", "geometry")};
    auto call_child = make_instruction(
        1,
        "call",
        {},
        {make_output_port("output", "geometry"), make_output_port("else", "geometry")});
    call_child.called_function_id = "failing-child";
    parent.instructions = {
        call_child,
        make_instruction(
            2,
            "fallback",
            {make_input_port("input", "string")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    parent.edges = {
        EdgeDescriptor{1, "else", 2, "input"},
        EdgeDescriptor{2, "output", 99, "result"},
    };
    parent.output_node_id = 99;

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "child_failure",
        [](const phoenix::InstructionExecutionFrame& frame) {
            phoenix::InstructionResult result;
            result.node_id = frame.inputs.node_id;
            result.failures = {
                phoenix::InstructionFailure{
                    frame.inputs.node_id,
                    std::nullopt,
                    "child failed",
                    {},
                    frame.call_stack,
                },
            };
            return result;
        });
    registry.register_handler(
        "fallback",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("parent-fallback")}},
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
        && result.failures.size() == 1
        && output_has_geometry_label(result, "result", "parent-fallback");
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

std::optional<phoenix::SeedValue> run_seed_probe(
    phoenix::NodeId node_id,
    std::optional<phoenix::SeedValue> local_seed,
    phoenix::FunctionCallPath call_path)
{
    FunctionDescriptor function;
    function.id = "seed-probe";
    auto instruction = make_instruction(
        node_id,
        "seed_probe",
        {},
        {make_output_port("output", "int")});
    instruction.local_seed = local_seed;
    function.instructions = {instruction};

    std::optional<phoenix::SeedValue> captured_seed;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "seed_probe",
        [&captured_seed](const phoenix::InstructionExecutionFrame& frame) {
            captured_seed = frame.effective_seed;
            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = std::move(call_path);
    request.context.global_seed = 42;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);
    if (result.status != phoenix::FunctionExecutionStatus::completed) {
        return std::nullopt;
    }

    return captured_seed;
}

bool test_effective_seed_changes_with_stable_identity_inputs()
{
    const auto base = run_seed_probe(7, std::nullopt, {"root", "seed-probe"});
    const auto repeat = run_seed_probe(7, std::nullopt, {"root", "seed-probe"});
    const auto different_node = run_seed_probe(8, std::nullopt, {"root", "seed-probe"});
    const auto different_path = run_seed_probe(7, std::nullopt, {"root", "other-call"});
    const auto local_seed = run_seed_probe(7, phoenix::SeedValue{5}, {"root", "seed-probe"});

    return base.has_value()
        && repeat.has_value()
        && different_node.has_value()
        && different_path.has_value()
        && local_seed.has_value()
        && base == repeat
        && base != different_node
        && base != different_path
        && base != local_seed;
}

bool test_multiplex_seed_modes_derive_item_seeds()
{
    FunctionDescriptor function;
    function.id = "item-seed-probe";
    auto shared_instruction = make_instruction(
        1,
        "shared_item_seed",
        {},
        {make_output_port("output", "int")});
    shared_instruction.multiplexes_input = true;
    shared_instruction.multiplex_seed_mode = phoenix::MultiplexSeedMode::one_seed_for_all;

    auto per_item_instruction = make_instruction(
        2,
        "per_item_seed",
        {},
        {make_output_port("output", "int")});
    per_item_instruction.multiplexes_input = true;
    per_item_instruction.multiplex_seed_mode = phoenix::MultiplexSeedMode::one_seed_each;

    function.instructions = {shared_instruction, per_item_instruction};

    std::optional<phoenix::SeedValue> shared_effective;
    std::optional<phoenix::SeedValue> shared_item_a;
    std::optional<phoenix::SeedValue> shared_item_b;
    std::optional<phoenix::SeedValue> per_item_effective;
    std::optional<phoenix::SeedValue> per_item_a;
    std::optional<phoenix::SeedValue> per_item_a_repeat;
    std::optional<phoenix::SeedValue> per_item_b;

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "shared_item_seed",
        [&shared_effective, &shared_item_a, &shared_item_b](const phoenix::InstructionExecutionFrame& frame) {
            shared_effective = frame.effective_seed;
            shared_item_a = frame.derive_item_seed(1);
            shared_item_b = frame.derive_item_seed(2);
            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });
    registry.register_handler(
        "per_item_seed",
        [&per_item_effective, &per_item_a, &per_item_a_repeat, &per_item_b](
            const phoenix::InstructionExecutionFrame& frame) {
            per_item_effective = frame.effective_seed;
            per_item_a = frame.derive_item_seed(1);
            per_item_a_repeat = frame.derive_item_seed(1);
            per_item_b = frame.derive_item_seed(2);
            return phoenix::InstructionResult{frame.inputs.node_id, {}, std::nullopt};
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root", "item-seed-probe"};
    request.context.global_seed = 42;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && shared_effective.has_value()
        && shared_item_a.has_value()
        && shared_item_b.has_value()
        && per_item_effective.has_value()
        && per_item_a.has_value()
        && per_item_a_repeat.has_value()
        && per_item_b.has_value()
        && shared_item_a == shared_effective
        && shared_item_b == shared_effective
        && per_item_a == per_item_a_repeat
        && per_item_a != per_item_b
        && per_item_a != per_item_effective;
}

bool test_top_level_execution_creates_root_actor()
{
    FunctionDescriptor function;
    function.id = "root-actor";
    function.instructions = {
        make_instruction(
            1,
            "root_geometry",
            {},
            {make_output_port("output", "geometry")}),
    };

    std::optional<phoenix::ActorId> frame_actor_id;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "root_geometry",
        [&frame_actor_id](const phoenix::InstructionExecutionFrame& frame) {
            const auto* current = frame.call_stack.current();
            if (current != nullptr) {
                frame_actor_id = current->actor_id;
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("root-geometry")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.actor.has_value()
        && result.actor->id == "actor:root"
        && frame_actor_id.has_value()
        && *frame_actor_id == result.actor->id
        && result.actor->children.empty()
        && !result.actor->geometry.has_value();
}

bool test_non_actor_nested_function_inherits_actor_context()
{
    FunctionDescriptor child;
    child.id = "child";
    child.instructions = {
        make_instruction(
            7,
            "child_geometry",
            {},
            {make_output_port("output", "geometry")}),
    };

    FunctionDescriptor parent;
    parent.id = "parent";
    auto call_child = make_instruction(
        2,
        "call",
        {},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    parent.instructions = {
        make_instruction(
            1,
            "parent_before",
            {},
            {make_output_port("output", "geometry")}),
        call_child,
        make_instruction(
            3,
            "parent_after",
            {},
            {make_output_port("output", "geometry")}),
    };

    std::optional<phoenix::ActorId> child_actor_id;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "parent_before",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("parent-before")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "child_geometry",
        [&child_actor_id](const phoenix::InstructionExecutionFrame& frame) {
            const auto* current = frame.call_stack.current();
            if (current != nullptr) {
                child_actor_id = current->actor_id;
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("child-geometry")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "parent_after",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("parent-after")}},
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
        && result.actor.has_value()
        && result.actor->id == "actor:root"
        && child_actor_id.has_value()
        && *child_actor_id == result.actor->id
        && result.actor->children.empty()
        && !result.actor->geometry.has_value();
}

bool test_actor_generating_nested_function_creates_child_actor()
{
    FunctionDescriptor child;
    child.id = "actor-child";
    child.generates_actor = true;
    auto child_geometry = make_instruction(
        7,
        "child_actor_geometry",
        {},
        {make_output_port("output", "geometry")});
    child_geometry.generates_actor = true;
    child.instructions = {child_geometry};

    FunctionDescriptor parent;
    parent.id = "actor-parent";
    auto call_child = make_instruction(
        2,
        "call",
        {},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    parent.instructions = {
        make_instruction(
            1,
            "parent_before_actor_child",
            {},
            {make_output_port("output", "geometry")}),
        call_child,
        make_instruction(
            3,
            "parent_after_actor_child",
            {},
            {make_output_port("output", "geometry")}),
    };

    std::optional<phoenix::ActorId> child_actor_id;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "parent_before_actor_child",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("parent-before")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "child_actor_geometry",
        [&child_actor_id](const phoenix::InstructionExecutionFrame& frame) {
            const auto* current = frame.call_stack.current();
            if (current != nullptr) {
                child_actor_id = current->actor_id;
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("child-actor-geometry")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "parent_after_actor_child",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("parent-after")}},
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
    const auto first = executor.run(request);
    const auto second = executor.run(request);

    return first.status == phoenix::FunctionExecutionStatus::completed
        && second.status == phoenix::FunctionExecutionStatus::completed
        && first.actor.has_value()
        && second.actor.has_value()
        && first.actor->id == "actor:root"
        && first.actor->children.size() == 1
        && second.actor->children.size() == 1
        && child_actor_id.has_value()
        && *child_actor_id == first.actor->children.front().id
        && first.actor->children.front().id == second.actor->children.front().id
        && first.actor->children.front().id == "actor:root:2:actor-child"
        && !first.actor->geometry.has_value()
        && !first.actor->children.front().geometry.has_value();
}

bool test_child_actor_geometry_keeps_owner_after_return_to_parent_graph()
{
    FunctionDescriptor child;
    child.id = "owned-child";
    child.generates_actor = true;
    child.output_ports = {make_output_port("result", "geometry")};
    child.instructions = {
        make_instruction(
            7,
            "child_owned_source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    child.edges = {EdgeDescriptor{7, "output", 99, "result"}};
    child.output_node_id = 99;

    FunctionDescriptor parent;
    parent.id = "owned-parent";
    parent.output_ports = {make_output_port("result", "geometry")};
    auto call_child = make_instruction(
        2,
        "call",
        {},
        {make_output_port("result", "geometry")});
    call_child.called_function_id = child.id;
    parent.instructions = {
        call_child,
        make_instruction(
            3,
            "parent_operates_on_child_geometry",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    parent.edges = {
        EdgeDescriptor{2, "result", 3, "input"},
        EdgeDescriptor{3, "output", 99, "result"},
    };
    parent.output_node_id = 99;

    std::optional<phoenix::ActorId> seen_input_owner;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "child_owned_source",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("child-owned")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "parent_operates_on_child_geometry",
        [&seen_input_owner](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            if (geometry != nullptr) {
                seen_input_owner = geometry->accumulation_actor_id;
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("parent-op-result")}},
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
        && result.actor.has_value()
        && result.actor->children.size() == 1
        && seen_input_owner.has_value()
        && *seen_input_owner == "actor:root:2:owned-child"
        && output_has_geometry_label(result, "result", "parent-op-result")
        && output_geometry_has_owner(result, "result", "actor:root:2:owned-child");
}

bool test_cross_actor_geometry_merge_fails()
{
    FunctionDescriptor child;
    child.id = "merge-child";
    child.generates_actor = true;
    child.output_ports = {make_output_port("result", "geometry")};
    child.instructions = {
        make_instruction(
            7,
            "merge_child_source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    child.edges = {EdgeDescriptor{7, "output", 99, "result"}};
    child.output_node_id = 99;

    FunctionDescriptor parent;
    parent.id = "cross-actor-merge-parent";
    auto first_call = make_instruction(
        2,
        "call_first",
        {},
        {make_output_port("result", "geometry")});
    first_call.called_function_id = child.id;
    auto second_call = make_instruction(
        3,
        "call_second",
        {},
        {make_output_port("result", "geometry")});
    second_call.called_function_id = child.id;
    parent.instructions = {
        first_call,
        second_call,
        make_instruction(
            4,
            "merge_cross_actor_geometry",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
    };
    parent.edges = {
        EdgeDescriptor{2, "result", 4, "input"},
        EdgeDescriptor{3, "result", 4, "input"},
    };

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "merge_child_source",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("child-owned")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "merge_cross_actor_geometry",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("should-not-run")}},
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

    return result.status == phoenix::FunctionExecutionStatus::failed
        && result.failure_message.has_value()
        && result.failure_message->find("different actor owners") != std::string::npos;
}

bool test_multiplexed_actor_generating_call_creates_child_actor_per_item()
{
    FunctionDescriptor child;
    child.id = "actor-item";
    child.input_ports = {make_input_port("input", "geometry")};
    child.generates_actor = true;
    auto child_capture = make_instruction(
        7,
        "capture_actor_item",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry")});
    child_capture.generates_actor = true;
    child.instructions = {child_capture};

    FunctionDescriptor parent;
    parent.id = "actor-items-parent";
    parent.input_ports = {make_input_port("input", "geometry")};
    auto call_child = make_instruction(
        2,
        "call",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    parent.instructions = {call_child};

    std::vector<phoenix::ActorId> captured_actor_ids;
    std::vector<std::string> captured_input_labels;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "capture_actor_item",
        [&captured_actor_ids, &captured_input_labels](const phoenix::InstructionExecutionFrame& frame) {
            const auto* current = frame.call_stack.current();
            if (current != nullptr && current->actor_id.has_value()) {
                captured_actor_ids.push_back(*current->actor_id);
            }

            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            if (geometry != nullptr) {
                captured_input_labels.push_back(geometry->debug_label);
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    PublicationTrace publication_trace;
    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
            phoenix::GeometryValue{"face-c"},
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto first = executor.run(request);
    const auto second = executor.run(request);

    return first.status == phoenix::FunctionExecutionStatus::completed
        && second.status == phoenix::FunctionExecutionStatus::completed
        && first.actor.has_value()
        && second.actor.has_value()
        && first.actor->children.size() == 3
        && second.actor->children.size() == 3
        && captured_actor_ids.size() == 6
        && captured_actor_ids[0] == "actor:root:2:actor-item:item:0"
        && captured_actor_ids[1] == "actor:root:2:actor-item:item:1"
        && captured_actor_ids[2] == "actor:root:2:actor-item:item:2"
        && first.actor->children[0].id == captured_actor_ids[0]
        && first.actor->children[1].id == captured_actor_ids[1]
        && first.actor->children[2].id == captured_actor_ids[2]
        && second.actor->children[0].id == first.actor->children[0].id
        && second.actor->children[1].id == first.actor->children[1].id
        && second.actor->children[2].id == first.actor->children[2].id
        && captured_input_labels.size() == 6
        && captured_input_labels[0] == "face-a"
        && captured_input_labels[1] == "face-b"
        && captured_input_labels[2] == "face-c";
}

bool test_worker_count_runs_multiplexed_child_items_concurrently()
{
    FunctionDescriptor child;
    child.id = "threaded-actor-item";
    child.input_ports = {make_input_port("input", "geometry")};
    child.generates_actor = true;
    auto child_capture = make_instruction(
        7,
        "threaded_capture_actor_item",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry")});
    child_capture.generates_actor = true;
    child.instructions = {child_capture};

    FunctionDescriptor parent;
    parent.id = "threaded-actor-items-parent";
    parent.input_ports = {make_input_port("input", "geometry")};
    auto call_child = make_instruction(
        2,
        "call",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    parent.instructions = {call_child};

    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    const auto record_overlap = [&active, &max_active]() {
        const int now_active = active.fetch_add(1) + 1;
        int observed = max_active.load();
        while (observed < now_active
            && !max_active.compare_exchange_weak(observed, now_active)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        active.fetch_sub(1);
    };

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "threaded_capture_actor_item",
        [&record_overlap](const phoenix::InstructionExecutionFrame& frame) {
            record_overlap();
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
            phoenix::GeometryValue{"face-c"},
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 3;

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && max_active.load() == 3
        && result.actor.has_value()
        && result.actor->children.size() == 3
        && result.actor->children[0].id == "actor:root:2:threaded-actor-item:item:0"
        && result.actor->children[1].id == "actor:root:2:threaded-actor-item:item:1"
        && result.actor->children[2].id == "actor:root:2:threaded-actor-item:item:2";
}

bool test_trace_request_keeps_multiplexed_child_items_serial()
{
    FunctionDescriptor child;
    child.id = "traced-actor-item";
    child.input_ports = {make_input_port("input", "geometry")};
    child.generates_actor = true;
    auto child_capture = make_instruction(
        7,
        "traced_capture_actor_item",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry")});
    child_capture.generates_actor = true;
    child.instructions = {child_capture};

    FunctionDescriptor parent;
    parent.id = "traced-actor-items-parent";
    parent.input_ports = {make_input_port("input", "geometry")};
    auto call_child = make_instruction(
        2,
        "call",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    parent.instructions = {call_child};

    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    const auto record_active = [&active, &max_active]() {
        const int now_active = active.fetch_add(1) + 1;
        int observed = max_active.load();
        while (observed < now_active
            && !max_active.compare_exchange_weak(observed, now_active)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        active.fetch_sub(1);
    };

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "traced_capture_actor_item",
        [&record_active](const phoenix::InstructionExecutionFrame& frame) {
            record_active();
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    PublicationTrace publication_trace;
    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
            phoenix::GeometryValue{"face-c"},
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 3;
    request.trace_level = phoenix::ExecutionTraceLevel::instruction;
    request.publication_trace_sink = &publication_trace;

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && max_active.load() == 1
        && result.actor.has_value()
        && result.actor->children.size() == 3;
}

bool test_multiplexed_actor_generation_routes_failed_item_without_actor()
{
    FunctionDescriptor child;
    child.id = "actor-item-with-failure";
    child.input_ports = {make_input_port("input", "geometry")};
    child.generates_actor = true;
    auto child_capture = make_instruction(
        7,
        "fail_one_actor_item",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry"), make_output_port("else", "geometry")});
    child_capture.generates_actor = true;
    child_capture.failure_is_critical = true;
    child.instructions = {child_capture};

    FunctionDescriptor parent;
    parent.id = "actor-items-fallback-parent";
    parent.input_ports = {make_input_port("input", "geometry")};
    parent.output_ports = {make_output_port("fallback", "geometry")};
    auto call_child = make_instruction(
        2,
        "call",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry"), make_output_port("else", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    parent.instructions = {
        call_child,
        make_instruction(
            3,
            "actor_item_fallback",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("fallback", "geometry")},
            {}),
    };
    parent.edges = {
        EdgeDescriptor{2, "else", 3, "input"},
        EdgeDescriptor{3, "output", 99, "fallback"},
    };
    parent.output_node_id = 99;

    bool fallback_saw_failed_item = false;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "fail_one_actor_item",
        [](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            if (geometry != nullptr && geometry->debug_label == "face-b") {
                phoenix::InstructionResult result;
                result.node_id = frame.inputs.node_id;
                result.failures = {
                    phoenix::InstructionFailure{
                        frame.inputs.node_id,
                        std::nullopt,
                        "item failed",
                        {phoenix::PortValue{"input", *input}},
                        frame.call_stack,
                    },
                };
                return result;
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "actor_item_fallback",
        [&fallback_saw_failed_item](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            fallback_saw_failed_item = geometry != nullptr && geometry->debug_label == "face-b";

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("fallback-for-face-b")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    PublicationTrace publication_trace;
    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
            phoenix::GeometryValue{"face-c"},
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};
    request.trace_level = phoenix::ExecutionTraceLevel::instruction;
    request.publication_trace_sink = &publication_trace;

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    bool parent_call_publication_matches = false;
    for (const auto& record : publication_trace.records) {
        if (record.function_id == parent.id
            && record.node_id == 2
            && record.actor_child_delta_count == 2
            && record.failure_count == 1) {
            parent_call_publication_matches = true;
        }
    }

    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.actor.has_value()
        && result.actor->children.size() == 2
        && result.actor->children[0].id == "actor:root:2:actor-item-with-failure:item:0"
        && result.actor->children[1].id == "actor:root:2:actor-item-with-failure:item:2"
        && result.failures.size() == 1
        && result.failures.front().item_key.has_value()
        && *result.failures.front().item_key == 1
        && fallback_saw_failed_item
        && output_has_geometry_label(result, "fallback", "fallback-for-face-b")
        && parent_call_publication_matches;
}

bool test_cached_multiplexed_actor_generation_commits_children_in_item_order()
{
    FunctionDescriptor child;
    child.id = "cached-actor-item";
    child.input_ports = {make_input_port("input", "geometry")};
    child.output_ports = {make_output_port("result", "geometry")};
    child.generates_actor = true;
    auto child_capture = make_instruction(
        7,
        "should_not_run_cached_actor_item",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry")});
    child_capture.generates_actor = true;
    child.instructions = {
        child_capture,
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    child.edges = {EdgeDescriptor{7, "output", 99, "result"}};
    child.output_node_id = 99;

    FunctionDescriptor parent;
    parent.id = "cached-actor-items-parent";
    parent.input_ports = {make_input_port("input", "geometry")};
    auto call_child = make_instruction(
        2,
        "call",
        {make_input_port("input", "geometry")},
        {make_output_port("result", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    parent.instructions = {call_child};

    const std::vector<const char*> labels{"face-a", "face-b", "face-c"};
    phoenix::MemoryCacheStore cache_store;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const phoenix::FunctionCallPath call_path{
            "root",
            "2:" + child.id,
            "item:" + std::to_string(i),
        };
        const auto identity = phoenix::CacheIdentityBuilder{}.identity(phoenix::CacheIdentityInput{
            &child,
            call_path,
            {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry(labels[i])}},
            77,
        });

        phoenix::FunctionCallCacheEntry entry;
        entry.key = phoenix::CacheKeyBuilder{}.function_call(
            phoenix::FunctionCallCacheKeyInput{identity});
        entry.outputs = {
            phoenix::PortValue{
                "result",
                phoenix::RuntimeValue::geometry(std::string{"cached-"} + labels[i])},
        };
        entry.actor = phoenix::ActorNode{};
        entry.actor->id = "actor:root:2:cached-actor-item:item:" + std::to_string(i);
        cache_store.put_function_call(entry);
    }

    int child_run_count = 0;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "should_not_run_cached_actor_item",
        [&child_run_count](const phoenix::InstructionExecutionFrame& frame) {
            ++child_run_count;
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("uncached")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    PublicationTrace publication_trace;
    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
            phoenix::GeometryValue{"face-c"},
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 77;
    request.trace_level = phoenix::ExecutionTraceLevel::instruction;
    request.publication_trace_sink = &publication_trace;
    request.cache_store = &cache_store;

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && child_run_count == 0
        && result.actor.has_value()
        && result.actor->children.size() == 3
        && result.actor->children[0].id == "actor:root:2:cached-actor-item:item:0"
        && result.actor->children[1].id == "actor:root:2:cached-actor-item:item:1"
        && result.actor->children[2].id == "actor:root:2:cached-actor-item:item:2"
        && publication_trace.records.size() == 1
        && publication_trace.records[0].node_id == 2
        && publication_trace.records[0].actor_child_delta_count == 3;
}

bool test_instancing_reuses_actor_generation_for_matching_explicit_keys()
{
    FunctionDescriptor child;
    child.id = "instanced-actor-item";
    child.input_ports = {
        make_input_port("input", "geometry"),
        make_input_port("instance_key", "string"),
    };
    child.generates_actor = true;
    auto child_capture = make_instruction(
        7,
        "capture_instanced_actor_item",
        {make_input_port("input", "geometry"), make_input_port("instance_key", "string")},
        {make_output_port("output", "geometry")});
    child_capture.generates_actor = true;
    child.instructions = {child_capture};

    FunctionDescriptor parent;
    parent.id = "instanced-actor-parent";
    parent.input_ports = {
        make_input_port("input", "geometry"),
        make_input_port("instance_key", "string"),
    };
    auto call_child = make_instruction(
        2,
        "call",
        {make_input_port("input", "geometry"), make_input_port("instance_key", "string")},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    call_child.enables_instancing = true;
    parent.instructions = {call_child};

    int child_run_count = 0;
    std::vector<std::string> captured_input_labels;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "capture_instanced_actor_item",
        [&child_run_count, &captured_input_labels](const phoenix::InstructionExecutionFrame& frame) {
            ++child_run_count;
            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            if (geometry != nullptr) {
                captured_input_labels.push_back(geometry->debug_label);
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
            phoenix::GeometryValue{"face-c"},
        })},
        phoenix::PortValue{"instance_key", phoenix::RuntimeValue::literal(phoenix::LiteralValue{
            phoenix::LiteralArray{
                phoenix::LiteralScalar{std::string{"same-topology"}},
                phoenix::LiteralScalar{std::string{"different-topology"}},
                phoenix::LiteralScalar{std::string{"same-topology"}},
            },
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.actor.has_value()
        && result.actor->children.size() == 3
        && child_run_count == 2
        && captured_input_labels.size() == 2
        && captured_input_labels[0] == "face-a"
        && captured_input_labels[1] == "face-b"
        && result.actor->children[0].id == "actor:root:2:instanced-actor-item:item:0"
        && result.actor->children[1].id == "actor:root:2:instanced-actor-item:item:1"
        && result.actor->children[2].id == "actor:root:2:instanced-actor-item:item:2"
        && result.actor->children[0].prototype.has_value()
        && result.actor->children[1].prototype.has_value()
        && result.actor->children[2].prototype.has_value()
        && result.actor->children[0].prototype->prototype_id
            == "prototype:instanced-actor-item:same-topology"
        && result.actor->children[1].prototype->prototype_id
            == "prototype:instanced-actor-item:different-topology"
        && result.actor->children[2].prototype->prototype_id
            == result.actor->children[0].prototype->prototype_id;
}

bool test_threaded_multiplex_instancing_dispatches_only_prototypes()
{
    FunctionDescriptor child;
    child.id = "threaded-instanced-actor-item";
    child.input_ports = {
        make_input_port("input", "geometry"),
        make_input_port("instance_key", "string"),
    };
    child.generates_actor = true;
    auto child_capture = make_instruction(
        7,
        "capture_threaded_instanced_actor_item",
        {make_input_port("input", "geometry"), make_input_port("instance_key", "string")},
        {make_output_port("output", "geometry")});
    child_capture.generates_actor = true;
    child.instructions = {child_capture};

    FunctionDescriptor parent;
    parent.id = "threaded-instanced-actor-parent";
    parent.input_ports = {
        make_input_port("input", "geometry"),
        make_input_port("instance_key", "string"),
    };
    auto call_child = make_instruction(
        2,
        "call",
        {make_input_port("input", "geometry"), make_input_port("instance_key", "string")},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    call_child.enables_instancing = true;
    parent.instructions = {call_child};

    std::atomic<int> child_run_count{0};
    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "capture_threaded_instanced_actor_item",
        [&child_run_count, &active, &max_active](const phoenix::InstructionExecutionFrame& frame) {
            ++child_run_count;
            const auto now_active = active.fetch_add(1) + 1;
            int observed = max_active.load();
            while (now_active > observed
                && !max_active.compare_exchange_weak(observed, now_active)) {
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            active.fetch_sub(1);
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
            phoenix::GeometryValue{"face-c"},
        })},
        phoenix::PortValue{"instance_key", phoenix::RuntimeValue::literal(phoenix::LiteralValue{
            phoenix::LiteralArray{
                phoenix::LiteralScalar{std::string{"same-topology"}},
                phoenix::LiteralScalar{std::string{"different-topology"}},
                phoenix::LiteralScalar{std::string{"same-topology"}},
            },
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 3;

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.actor.has_value()
        && result.actor->children.size() == 3
        && child_run_count == 2
        && max_active == 2
        && result.actor->children[0].prototype.has_value()
        && result.actor->children[1].prototype.has_value()
        && result.actor->children[2].prototype.has_value()
        && result.actor->children[0].prototype->prototype_id
            == "prototype:threaded-instanced-actor-item:same-topology"
        && result.actor->children[1].prototype->prototype_id
            == "prototype:threaded-instanced-actor-item:different-topology"
        && result.actor->children[2].prototype->prototype_id
            == result.actor->children[0].prototype->prototype_id;
}

bool test_diagnostics_records_multiplex_instancing_counts()
{
    FunctionDescriptor child;
    child.id = "diagnosed-instanced-actor-item";
    child.input_ports = {
        make_input_port("input", "geometry"),
        make_input_port("instance_key", "string"),
    };
    child.generates_actor = true;
    auto child_capture = make_instruction(
        7,
        "capture_diagnosed_instanced_actor_item",
        {make_input_port("input", "geometry"), make_input_port("instance_key", "string")},
        {make_output_port("output", "geometry")});
    child_capture.generates_actor = true;
    child.instructions = {child_capture};

    FunctionDescriptor parent;
    parent.id = "diagnosed-instanced-actor-parent";
    parent.input_ports = {
        make_input_port("input", "geometry"),
        make_input_port("instance_key", "string"),
    };
    auto call_child = make_instruction(
        2,
        "call",
        {make_input_port("input", "geometry"), make_input_port("instance_key", "string")},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    call_child.enables_instancing = true;
    parent.instructions = {call_child};

    int child_run_count = 0;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "capture_diagnosed_instanced_actor_item",
        [&child_run_count](const phoenix::InstructionExecutionFrame& frame) {
            ++child_run_count;
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    DiagnosticsTrace diagnostics;
    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
            phoenix::GeometryValue{"face-c"},
            phoenix::GeometryValue{"face-d"},
        })},
        phoenix::PortValue{"instance_key", phoenix::RuntimeValue::literal(phoenix::LiteralValue{
            phoenix::LiteralArray{
                phoenix::LiteralScalar{std::string{"same-topology"}},
                phoenix::LiteralScalar{std::string{"other-topology"}},
                phoenix::LiteralScalar{std::string{"same-topology"}},
                phoenix::LiteralScalar{std::string{"same-topology"}},
            },
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 3;
    request.diagnostics_sink = &diagnostics;

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    bool parent_call_diagnostics_match = false;
    for (const auto& record : diagnostics.records) {
        if (record.function_id == parent.id
            && record.node_id == 2
            && record.multiplex_item_count == 4
            && record.multiplex_prototype_work_count == 2
            && record.multiplex_reused_instance_count == 2
            && record.actor_child_delta_count == 4) {
            parent_call_diagnostics_match = true;
        }
    }

    return result.status == phoenix::FunctionExecutionStatus::completed
        && child_run_count == 2
        && parent_call_diagnostics_match;
}

bool test_threaded_multiplex_instancing_is_reproducible_under_repeated_runs()
{
    FunctionDescriptor child;
    child.id = "repeated-threaded-instanced-actor-item";
    child.input_ports = {
        make_input_port("input", "geometry"),
        make_input_port("instance_key", "string"),
    };
    child.generates_actor = true;
    auto child_capture = make_instruction(
        7,
        "capture_repeated_threaded_instanced_actor_item",
        {make_input_port("input", "geometry"), make_input_port("instance_key", "string")},
        {make_output_port("output", "geometry")});
    child_capture.generates_actor = true;
    child.instructions = {child_capture};

    FunctionDescriptor parent;
    parent.id = "repeated-threaded-instanced-actor-parent";
    parent.input_ports = {
        make_input_port("input", "geometry"),
        make_input_port("instance_key", "string"),
    };
    auto call_child = make_instruction(
        2,
        "call",
        {make_input_port("input", "geometry"), make_input_port("instance_key", "string")},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    call_child.enables_instancing = true;
    parent.instructions = {call_child};

    std::atomic<int> child_run_count{0};
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "capture_repeated_threaded_instanced_actor_item",
        [&child_run_count](const phoenix::InstructionExecutionFrame& frame) {
            ++child_run_count;
            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            const auto sleep_ms = geometry != nullptr && geometry->debug_label == "face-a"
                ? 35
                : geometry != nullptr && geometry->debug_label == "face-b" ? 20 : 5;
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
            phoenix::GeometryValue{"face-c"},
            phoenix::GeometryValue{"face-d"},
            phoenix::GeometryValue{"face-e"},
            phoenix::GeometryValue{"face-f"},
        })},
        phoenix::PortValue{"instance_key", phoenix::RuntimeValue::literal(phoenix::LiteralValue{
            phoenix::LiteralArray{
                phoenix::LiteralScalar{std::string{"topology-a"}},
                phoenix::LiteralScalar{std::string{"topology-b"}},
                phoenix::LiteralScalar{std::string{"topology-a"}},
                phoenix::LiteralScalar{std::string{"topology-c"}},
                phoenix::LiteralScalar{std::string{"topology-b"}},
                phoenix::LiteralScalar{std::string{"topology-a"}},
            },
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 1234;
    request.options.worker_count = 4;

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto first = executor.run(request);
    if (first.status != phoenix::FunctionExecutionStatus::completed
        || !first.actor.has_value()
        || first.actor->children.size() != 6) {
        return false;
    }

    for (int i = 0; i < 20; ++i) {
        const auto next = executor.run(request);
        if (!execution_results_observably_equal(first, next)) {
            return false;
        }
    }

    return child_run_count == 63
        && first.actor->children[0].prototype.has_value()
        && first.actor->children[1].prototype.has_value()
        && first.actor->children[2].prototype.has_value()
        && first.actor->children[3].prototype.has_value()
        && first.actor->children[4].prototype.has_value()
        && first.actor->children[5].prototype.has_value()
        && first.actor->children[0].prototype->prototype_id
            == "prototype:repeated-threaded-instanced-actor-item:topology-a"
        && first.actor->children[1].prototype->prototype_id
            == "prototype:repeated-threaded-instanced-actor-item:topology-b"
        && first.actor->children[2].prototype->prototype_id
            == first.actor->children[0].prototype->prototype_id
        && first.actor->children[3].prototype->prototype_id
            == "prototype:repeated-threaded-instanced-actor-item:topology-c"
        && first.actor->children[4].prototype->prototype_id
            == first.actor->children[1].prototype->prototype_id
        && first.actor->children[5].prototype->prototype_id
            == first.actor->children[0].prototype->prototype_id;
}

bool test_threaded_multiplex_instancing_respects_worker_count_for_prototypes()
{
    FunctionDescriptor child;
    child.id = "bounded-threaded-instanced-actor-item";
    child.input_ports = {
        make_input_port("input", "geometry"),
        make_input_port("instance_key", "string"),
    };
    child.generates_actor = true;
    auto child_capture = make_instruction(
        7,
        "capture_bounded_threaded_instanced_actor_item",
        {make_input_port("input", "geometry"), make_input_port("instance_key", "string")},
        {make_output_port("output", "geometry")});
    child_capture.generates_actor = true;
    child.instructions = {child_capture};

    FunctionDescriptor parent;
    parent.id = "bounded-threaded-instanced-actor-parent";
    parent.input_ports = {
        make_input_port("input", "geometry"),
        make_input_port("instance_key", "string"),
    };
    auto call_child = make_instruction(
        2,
        "call",
        {make_input_port("input", "geometry"), make_input_port("instance_key", "string")},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    call_child.enables_instancing = true;
    parent.instructions = {call_child};

    std::atomic<int> child_run_count{0};
    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "capture_bounded_threaded_instanced_actor_item",
        [&child_run_count, &active, &max_active](const phoenix::InstructionExecutionFrame& frame) {
            ++child_run_count;
            const auto now_active = active.fetch_add(1) + 1;
            int observed = max_active.load();
            while (now_active > observed
                && !max_active.compare_exchange_weak(observed, now_active)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            active.fetch_sub(1);
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
            phoenix::GeometryValue{"face-c"},
            phoenix::GeometryValue{"face-d"},
            phoenix::GeometryValue{"face-e"},
        })},
        phoenix::PortValue{"instance_key", phoenix::RuntimeValue::literal(phoenix::LiteralValue{
            phoenix::LiteralArray{
                phoenix::LiteralScalar{std::string{"topology-a"}},
                phoenix::LiteralScalar{std::string{"topology-b"}},
                phoenix::LiteralScalar{std::string{"topology-c"}},
                phoenix::LiteralScalar{std::string{"topology-b"}},
                phoenix::LiteralScalar{std::string{"topology-a"}},
            },
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 2;

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.actor.has_value()
        && result.actor->children.size() == 5
        && child_run_count == 3
        && max_active == 2;
}

bool test_threaded_multiplex_failures_are_canonical_despite_completion_order()
{
    FunctionDescriptor child;
    child.id = "threaded-failing-actor-item";
    child.input_ports = {make_input_port("input", "geometry")};
    child.generates_actor = true;
    auto child_capture = make_instruction(
        7,
        "threaded_fail_one_actor_item",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry")});
    child_capture.generates_actor = true;
    child_capture.failure_is_critical = true;
    child.instructions = {child_capture};

    FunctionDescriptor parent;
    parent.id = "threaded-failing-actor-parent";
    parent.input_ports = {make_input_port("input", "geometry")};
    parent.output_ports = {make_output_port("fallback", "geometry")};
    auto call_child = make_instruction(
        2,
        "call",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry"), make_output_port("else", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    parent.instructions = {
        call_child,
        make_instruction(
            3,
            "threaded_actor_item_fallback",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("fallback", "geometry")},
            {}),
    };
    parent.edges = {
        EdgeDescriptor{2, "else", 3, "input"},
        EdgeDescriptor{3, "output", 99, "fallback"},
    };
    parent.output_node_id = 99;

    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    bool fallback_saw_failed_item = false;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "threaded_fail_one_actor_item",
        [&active, &max_active](const phoenix::InstructionExecutionFrame& frame) {
            const auto now_active = active.fetch_add(1) + 1;
            int observed = max_active.load();
            while (now_active > observed
                && !max_active.compare_exchange_weak(observed, now_active)) {
            }

            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            const auto sleep_ms = geometry != nullptr && geometry->debug_label == "face-a"
                ? 60
                : geometry != nullptr && geometry->debug_label == "face-b"
                    ? 5
                    : geometry != nullptr && geometry->debug_label == "face-c" ? 30 : 15;
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            active.fetch_sub(1);

            if (geometry != nullptr && geometry->debug_label == "face-b") {
                phoenix::InstructionResult result;
                result.node_id = frame.inputs.node_id;
                result.failures = {
                    phoenix::InstructionFailure{
                        frame.inputs.node_id,
                        std::nullopt,
                        "threaded item failed",
                        {phoenix::PortValue{"input", *input}},
                        frame.call_stack,
                    },
                };
                return result;
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "threaded_actor_item_fallback",
        [&fallback_saw_failed_item](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            fallback_saw_failed_item = geometry != nullptr && geometry->debug_label == "face-b";

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("fallback-face-b")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
            phoenix::GeometryValue{"face-c"},
            phoenix::GeometryValue{"face-d"},
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};
    request.options.worker_count = 4;

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && max_active == 4
        && result.actor.has_value()
        && result.actor->children.size() == 3
        && result.actor->children[0].id == "actor:root:2:threaded-failing-actor-item:item:0"
        && result.actor->children[1].id == "actor:root:2:threaded-failing-actor-item:item:2"
        && result.actor->children[2].id == "actor:root:2:threaded-failing-actor-item:item:3"
        && result.failures.size() == 1
        && result.failures.front().item_key.has_value()
        && *result.failures.front().item_key == 1
        && fallback_saw_failed_item
        && output_has_geometry_label(result, "fallback", "fallback-face-b");
}

bool test_instancing_does_not_reuse_without_explicit_keys()
{
    FunctionDescriptor child;
    child.id = "unkeyed-instanced-actor-item";
    child.input_ports = {make_input_port("input", "geometry")};
    child.generates_actor = true;
    auto child_capture = make_instruction(
        7,
        "capture_unkeyed_instanced_actor_item",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry")});
    child_capture.generates_actor = true;
    child.instructions = {child_capture};

    FunctionDescriptor parent;
    parent.id = "unkeyed-instanced-actor-parent";
    parent.input_ports = {make_input_port("input", "geometry")};
    auto call_child = make_instruction(
        2,
        "call",
        {make_input_port("input", "geometry")},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    call_child.multiplexes_input = true;
    call_child.enables_instancing = true;
    parent.instructions = {call_child};

    int child_run_count = 0;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "capture_unkeyed_instanced_actor_item",
        [&child_run_count](const phoenix::InstructionExecutionFrame& frame) {
            ++child_run_count;
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("ignored")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.inputs = {
        phoenix::PortValue{"input", phoenix::RuntimeValue::geometry_collection({
            phoenix::GeometryValue{"face-a"},
            phoenix::GeometryValue{"face-b"},
            phoenix::GeometryValue{"face-c"},
        })},
    };
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};

    const phoenix::FunctionExecutor executor(registry, functions);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.actor.has_value()
        && result.actor->children.size() == 3
        && child_run_count == 3
        && !result.actor->children[0].prototype.has_value()
        && !result.actor->children[1].prototype.has_value()
        && !result.actor->children[2].prototype.has_value();
}

bool test_execution_populates_cache_entries()
{
    FunctionDescriptor function;
    function.id = "cache-published";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            1,
            "cache_source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {
        EdgeDescriptor{1, "output", 99, "result"},
    };
    function.output_node_id = 99;

    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "cache_source",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("cached-output")}},
                std::nullopt,
            };
        });

    phoenix::MemoryCacheStore cache_store;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 123;
    request.cache_writer = &cache_store;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    const phoenix::CacheIdentityBuilder identity_builder;
    const auto identity = identity_builder.identity(phoenix::CacheIdentityInput{
        &function,
        {"root"},
        {},
        123,
    });

    const phoenix::SeedDeriver seed_deriver;
    phoenix::SeedDerivationInput seed_input;
    seed_input.global_seed = 123;
    seed_input.call_path = {"root"};
    seed_input.node_id = 1;

    const phoenix::CacheKeyBuilder key_builder;
    phoenix::InstructionCacheKeyInput instruction_key_input;
    instruction_key_input.identity = identity;
    instruction_key_input.node_id = 1;
    instruction_key_input.effective_seed = seed_deriver.derive(seed_input);

    phoenix::ActorSubtreeCacheKeyInput subtree_key_input;
    subtree_key_input.identity = identity;
    subtree_key_input.actor_id = "actor:root";

    const auto instruction = cache_store.find_instruction(
        key_builder.instruction_outputs(instruction_key_input));
    const auto function_call = cache_store.find_function_call(
        key_builder.function_call(phoenix::FunctionCallCacheKeyInput{identity}));
    const auto actor_subtree = cache_store.find_actor_subtree(
        key_builder.actor_subtree(subtree_key_input));
    const auto* instruction_geometry = instruction.has_value() && !instruction->outputs.empty()
        ? instruction->outputs.front().value.as_geometry()
        : nullptr;
    const auto* function_geometry = function_call.has_value() && !function_call->outputs.empty()
        ? function_call->outputs.front().value.as_geometry()
        : nullptr;

    return result.status == phoenix::FunctionExecutionStatus::completed
        && instruction_geometry != nullptr
        && instruction_geometry->debug_label == "cached-output"
        && function_geometry != nullptr
        && function_geometry->debug_label == "cached-output"
        && actor_subtree.has_value()
        && actor_subtree->actor.id == "actor:root";
}

bool test_execution_uses_function_call_cache_hit()
{
    FunctionDescriptor function;
    function.id = "cache-read-function";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            1,
            "should_not_run_function_cache",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {EdgeDescriptor{1, "output", 99, "result"}};
    function.output_node_id = 99;

    const phoenix::CacheIdentity identity = phoenix::CacheIdentityBuilder{}.identity(
        phoenix::CacheIdentityInput{&function, {"root"}, {}, 5});
    phoenix::MemoryCacheStore cache_store;
    phoenix::FunctionCallCacheEntry entry;
    entry.key = phoenix::CacheKeyBuilder{}.function_call(phoenix::FunctionCallCacheKeyInput{identity});
    entry.outputs = {
        phoenix::PortValue{"result", phoenix::RuntimeValue::geometry("from-function-cache")},
    };
    entry.actor = phoenix::ActorNode{};
    entry.actor->id = "actor:cached";
    cache_store.put_function_call(entry);

    int run_count = 0;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "should_not_run_function_cache",
        [&run_count](const phoenix::InstructionExecutionFrame& frame) {
            ++run_count;
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("uncached")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 5;
    request.cache_store = &cache_store;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && run_count == 0
        && output_has_geometry_label(result, "result", "from-function-cache")
        && result.actor.has_value()
        && result.actor->id == "actor:cached";
}

bool test_execution_uses_instruction_cache_hit()
{
    FunctionDescriptor function;
    function.id = "cache-read-instruction";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            1,
            "should_not_run_instruction_cache",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            2,
            "downstream_after_cache",
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

    const auto identity = phoenix::CacheIdentityBuilder{}.identity(
        phoenix::CacheIdentityInput{&function, {"root"}, {}, 9});
    phoenix::SeedDerivationInput seed_input;
    seed_input.global_seed = 9;
    seed_input.call_path = {"root"};
    seed_input.node_id = 1;

    phoenix::InstructionCacheKeyInput key_input;
    key_input.identity = identity;
    key_input.node_id = 1;
    key_input.effective_seed = phoenix::SeedDeriver{}.derive(seed_input);

    phoenix::MemoryCacheStore cache_store;
    phoenix::InstructionCacheEntry entry;
    entry.key = phoenix::CacheKeyBuilder{}.instruction_outputs(key_input);
    entry.outputs = {
        phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("from-instruction-cache")},
    };
    cache_store.put_instruction(entry);

    int source_run_count = 0;
    bool downstream_saw_cached_input = false;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "should_not_run_instruction_cache",
        [&source_run_count](const phoenix::InstructionExecutionFrame& frame) {
            ++source_run_count;
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("uncached")}},
                std::nullopt,
            };
        });
    registry.register_handler(
        "downstream_after_cache",
        [&downstream_saw_cached_input](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            downstream_saw_cached_input =
                geometry != nullptr && geometry->debug_label == "from-instruction-cache";
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("downstream")}},
                std::nullopt,
            };
        });

    PublicationTrace publication_trace;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 9;
    request.trace_level = phoenix::ExecutionTraceLevel::instruction;
    request.publication_trace_sink = &publication_trace;
    request.cache_store = &cache_store;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return result.status == phoenix::FunctionExecutionStatus::completed
        && source_run_count == 0
        && downstream_saw_cached_input
        && output_has_geometry_label(result, "result", "downstream")
        && publication_trace.records.size() == 2
        && publication_trace.records[0].node_id == 1
        && publication_trace.records[0].instruction_cache_hit
        && publication_trace.records[1].node_id == 2
        && !publication_trace.records[1].instruction_cache_hit;
}

bool test_function_call_cache_hit_matches_full_execution()
{
    FunctionDescriptor child;
    child.id = "cache-equivalent-child";
    child.generates_actor = true;
    child.instructions = {
        make_instruction(
            7,
            "cache_equivalent_child_geometry",
            {},
            {make_output_port("output", "geometry")}),
    };

    FunctionDescriptor parent;
    parent.id = "cache-equivalent-parent";
    auto call_child = make_instruction(
        2,
        "call",
        {},
        {make_output_port("output", "geometry")});
    call_child.called_function_id = child.id;
    parent.instructions = {call_child};

    phoenix::InstructionRegistry full_registry;
    full_registry.register_handler(
        "cache_equivalent_child_geometry",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("child-cache-shape")}},
                std::nullopt,
            };
        });

    phoenix::FunctionLibrary functions;
    functions.register_function(parent);
    functions.register_function(child);

    phoenix::MemoryCacheStore cache_store;
    phoenix::FunctionExecutionRequest request;
    request.function = &parent;
    request.context.function_id = parent.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 31;
    request.cache_writer = &cache_store;

    const phoenix::FunctionExecutor full_executor(full_registry, functions);
    const auto full_result = full_executor.run(request);

    int cached_run_count = 0;
    phoenix::InstructionRegistry cached_registry;
    cached_registry.register_handler(
        "cache_equivalent_child_geometry",
        [&cached_run_count](const phoenix::InstructionExecutionFrame& frame) {
            ++cached_run_count;
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("should-not-run")}},
                std::nullopt,
            };
        });

    request.cache_writer = nullptr;
    request.cache_store = &cache_store;
    const phoenix::FunctionExecutor cached_executor(cached_registry, functions);
    const auto cached_result = cached_executor.run(request);

    return cached_run_count == 0
        && full_result.actor.has_value()
        && full_result.actor->children.size() == 1
        && execution_results_observably_equal(full_result, cached_result);
}

bool test_instruction_cache_hit_matches_full_execution()
{
    FunctionDescriptor function;
    function.id = "cache-equivalent-instruction";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            1,
            "cache_equivalent_source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            2,
            "cache_equivalent_downstream",
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

    phoenix::InstructionRegistry full_registry;
    full_registry.register_handler(
        "cache_equivalent_source",
        [](const phoenix::InstructionExecutionFrame& frame) {
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("source-equivalent")}},
                std::nullopt,
            };
        });
    full_registry.register_handler(
        "cache_equivalent_downstream",
        [](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            if (geometry == nullptr || geometry->debug_label != "source-equivalent") {
                return phoenix::InstructionResult{
                    frame.inputs.node_id,
                    {},
                    "downstream did not receive source geometry",
                };
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("downstream-equivalent")}},
                std::nullopt,
            };
        });

    phoenix::MemoryCacheStore cache_store;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 41;
    request.cache_writer = &cache_store;

    const phoenix::FunctionExecutor full_executor(full_registry);
    const auto full_result = full_executor.run(request);

    const auto identity = phoenix::CacheIdentityBuilder{}.identity(phoenix::CacheIdentityInput{
        &function,
        {"root"},
        {},
        41,
    });
    const phoenix::CacheKeyBuilder key_builder;
    const bool removed_function_call = cache_store.remove_function_call(
        key_builder.function_call(phoenix::FunctionCallCacheKeyInput{identity}));

    phoenix::SeedDerivationInput seed_input;
    seed_input.global_seed = 41;
    seed_input.call_path = {"root"};
    seed_input.node_id = 2;

    phoenix::InstructionCacheKeyInput instruction_key_input;
    instruction_key_input.identity = identity;
    instruction_key_input.node_id = 2;
    instruction_key_input.effective_seed = phoenix::SeedDeriver{}.derive(seed_input);
    const bool removed_downstream_instruction =
        cache_store.remove_instruction(key_builder.instruction_outputs(instruction_key_input));

    int source_run_count = 0;
    int downstream_run_count = 0;
    phoenix::InstructionRegistry cached_registry;
    cached_registry.register_handler(
        "cache_equivalent_source",
        [&source_run_count](const phoenix::InstructionExecutionFrame& frame) {
            ++source_run_count;
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("should-not-run")}},
                std::nullopt,
            };
        });
    cached_registry.register_handler(
        "cache_equivalent_downstream",
        [&downstream_run_count](const phoenix::InstructionExecutionFrame& frame) {
            ++downstream_run_count;
            const auto* input = find_input(frame, "input");
            const auto* geometry = input == nullptr ? nullptr : input->as_geometry();
            if (geometry == nullptr || geometry->debug_label != "source-equivalent") {
                return phoenix::InstructionResult{
                    frame.inputs.node_id,
                    {},
                    "downstream did not receive cached source geometry",
                };
            }

            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("downstream-equivalent")}},
                std::nullopt,
            };
        });

    request.cache_writer = nullptr;
    request.cache_store = &cache_store;
    const phoenix::FunctionExecutor cached_executor(cached_registry);
    const auto cached_result = cached_executor.run(request);

    return removed_function_call
        && removed_downstream_instruction
        && source_run_count == 0
        && downstream_run_count == 1
        && execution_results_observably_equal(full_result, cached_result);
}

bool test_execution_ignores_function_cache_with_wrong_identity()
{
    FunctionDescriptor function;
    function.id = "cache-wrong-identity";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            1,
            "wrong_identity_source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {EdgeDescriptor{1, "output", 99, "result"}};
    function.output_node_id = 99;

    const auto stale_identity = phoenix::CacheIdentityBuilder{}.identity(phoenix::CacheIdentityInput{
        &function,
        {"root"},
        {},
        1,
    });

    phoenix::MemoryCacheStore cache_store;
    phoenix::FunctionCallCacheEntry stale_entry;
    stale_entry.key = phoenix::CacheKeyBuilder{}.function_call(
        phoenix::FunctionCallCacheKeyInput{stale_identity});
    stale_entry.outputs = {
        phoenix::PortValue{"result", phoenix::RuntimeValue::geometry("stale-cache")},
    };
    cache_store.put_function_call(stale_entry);

    int run_count = 0;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "wrong_identity_source",
        [&run_count](const phoenix::InstructionExecutionFrame& frame) {
            ++run_count;
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("fresh-run")}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 2;
    request.cache_store = &cache_store;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);

    return run_count == 1
        && result.status == phoenix::FunctionExecutionStatus::completed
        && output_has_geometry_label(result, "result", "fresh-run");
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
    ok = run_test("ready frontier admits downstream after each completion", test_ready_frontier_admits_downstream_after_each_completion) && ok;
    ok = run_test("downstream waits for all geometry contributions", test_downstream_waits_for_all_geometry_contributions) && ok;
    ok = run_test("worker count runs independent handlers concurrently", test_worker_count_runs_independent_handlers_concurrently) && ok;
    ok = run_test("threaded regular handlers keep traces ordered", test_threaded_regular_handlers_keep_traces_ordered) && ok;
    ok = run_test("worker count preserves observable results", test_worker_count_preserves_observable_results) && ok;
    ok = run_test("threaded publication order ignores completion order", test_threaded_publication_order_ignores_completion_order) && ok;
    ok = run_test("threaded handler failure uses central publication", test_threaded_handler_failure_uses_central_publication) && ok;
    ok = run_test("force run remains serial with worker count", test_force_run_remains_serial_with_worker_count) && ok;
    ok = run_test("cache request keeps regular handlers serial", test_cache_request_keeps_regular_handlers_serial) && ok;
    ok = run_test("diagnostics records threaded instruction metrics in publication order", test_diagnostics_records_threaded_instruction_metrics_in_publication_order) && ok;
    ok = run_test("diagnostics records cache hits and serial mode", test_diagnostics_records_cache_hits_and_serial_mode) && ok;
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
    ok = run_test("noncritical unhandled failure is logged and execution continues", test_noncritical_unhandled_failure_is_logged_and_execution_continues) && ok;
    ok = run_test("critical unhandled failure fails function", test_critical_unhandled_failure_fails_function) && ok;
    ok = run_test("handled failure routes failed item through else", test_handled_failure_routes_failed_item_through_else) && ok;
    ok = run_test("multiplexed successes and failures accumulate", test_multiplexed_successes_and_failures_accumulate) && ok;
    ok = run_test("nested function failure can be handled by call else", test_nested_function_failure_can_be_handled_by_call_else) && ok;
    ok = run_test("effective seed is deterministic", test_effective_seed_is_deterministic) && ok;
    ok = run_test("effective seed changes with stable identity inputs", test_effective_seed_changes_with_stable_identity_inputs) && ok;
    ok = run_test("multiplex seed modes derive item seeds", test_multiplex_seed_modes_derive_item_seeds) && ok;
    ok = run_test("top-level execution creates root actor", test_top_level_execution_creates_root_actor) && ok;
    ok = run_test("non-actor nested function inherits actor context", test_non_actor_nested_function_inherits_actor_context) && ok;
    ok = run_test("actor-generating nested function creates child actor", test_actor_generating_nested_function_creates_child_actor) && ok;
    ok = run_test("child actor geometry keeps owner after return to parent graph", test_child_actor_geometry_keeps_owner_after_return_to_parent_graph) && ok;
    ok = run_test("cross actor geometry merge fails", test_cross_actor_geometry_merge_fails) && ok;
    ok = run_test("multiplexed actor-generating call creates child actor per item", test_multiplexed_actor_generating_call_creates_child_actor_per_item) && ok;
    ok = run_test("worker count runs multiplexed child items concurrently", test_worker_count_runs_multiplexed_child_items_concurrently) && ok;
    ok = run_test("trace request keeps multiplexed child items serial", test_trace_request_keeps_multiplexed_child_items_serial) && ok;
    ok = run_test("multiplexed actor generation routes failed item without actor", test_multiplexed_actor_generation_routes_failed_item_without_actor) && ok;
    ok = run_test("cached multiplexed actor generation commits children in item order", test_cached_multiplexed_actor_generation_commits_children_in_item_order) && ok;
    ok = run_test("instancing reuses actor generation for matching explicit keys", test_instancing_reuses_actor_generation_for_matching_explicit_keys) && ok;
    ok = run_test("threaded multiplex instancing dispatches only prototypes", test_threaded_multiplex_instancing_dispatches_only_prototypes) && ok;
    ok = run_test("diagnostics records multiplex instancing counts", test_diagnostics_records_multiplex_instancing_counts) && ok;
    ok = run_test("threaded multiplex instancing is reproducible under repeated runs", test_threaded_multiplex_instancing_is_reproducible_under_repeated_runs) && ok;
    ok = run_test("threaded multiplex instancing respects worker count for prototypes", test_threaded_multiplex_instancing_respects_worker_count_for_prototypes) && ok;
    ok = run_test("threaded multiplex failures are canonical despite completion order", test_threaded_multiplex_failures_are_canonical_despite_completion_order) && ok;
    ok = run_test("instancing does not reuse without explicit keys", test_instancing_does_not_reuse_without_explicit_keys) && ok;
    ok = run_test("execution populates cache entries", test_execution_populates_cache_entries) && ok;
    ok = run_test("execution uses function call cache hit", test_execution_uses_function_call_cache_hit) && ok;
    ok = run_test("execution uses instruction cache hit", test_execution_uses_instruction_cache_hit) && ok;
    ok = run_test("function call cache hit matches full execution", test_function_call_cache_hit_matches_full_execution) && ok;
    ok = run_test("instruction cache hit matches full execution", test_instruction_cache_hit_matches_full_execution) && ok;
    ok = run_test("execution ignores function cache with wrong identity", test_execution_ignores_function_cache_with_wrong_identity) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
