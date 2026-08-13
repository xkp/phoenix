#include "phoenix/loop/function_body.hpp"

#include <iostream>

namespace {

phoenix::FunctionDescriptor body_function()
{
    using namespace phoenix;
    FunctionDescriptor function;
    function.id = "loop-body";
    function.input_ports = {
        {"input", "literal", PortDirection::input},
        {"$index", "integer", PortDirection::input}};
    function.output_ports = {
        {"loop", "literal", PortDirection::output},
        {"all", "literal", PortDirection::output},
        {"output", "literal", PortDirection::output}};
    InstructionDescriptor body;
    body.id = 1;
    body.kind = "loop-test-body";
    body.input_ports = function.input_ports;
    body.output_ports = function.output_ports;
    InstructionDescriptor output;
    output.id = 99;
    output.kind = "output";
    output.input_ports = function.output_ports;
    function.instructions = {body, output};
    function.edges = {
        {1, "loop", 99, "loop"}, {1, "all", 99, "all"},
        {1, "output", 99, "output"}};
    function.output_node_id = 99;
    return function;
}

std::int64_t integer(const phoenix::RuntimeValue& value)
{
    const auto* literal = value.as_literal();
    const auto scalar = literal ? phoenix::literal_first_scalar(*literal) : std::nullopt;
    const auto* result = scalar ? std::get_if<std::int64_t>(&*scalar) : nullptr;
    return result ? *result : -1;
}

const phoenix::RuntimeValue* input(const phoenix::InstructionExecutionFrame& frame,
    const phoenix::PortId& port)
{
    for (const auto& value : frame.inputs.promised_inputs)
        if (value.port == port) return &value.value;
    return nullptr;
}

bool nested_body_routes_feedback_index_and_precedence()
{
    using namespace phoenix;
    std::vector<FunctionCallPath> paths;
    InstructionRegistry registry;
    registry.register_handler("loop-test-body", [&paths](const auto& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        paths.push_back(frame.context.call_path);
        const auto index = integer(*input(frame, "$index"));
        result.produced_outputs.push_back({"all", RuntimeValue::literal(index + 100)});
        result.produced_outputs.push_back({"output", RuntimeValue::literal(index + 200)});
        if (index < 2)
            result.produced_outputs.push_back({"loop", *input(frame, "input")});
        return result;
    });
    const auto function = body_function();
    const FunctionExecutor executor{registry};
    loop::FunctionBodyRequest body_request;
    body_request.executor = &executor;
    body_request.function = &function;
    body_request.loop_node_id = 8;
    body_request.execution.context.call_path = {"root"};
    loop::RunRequest request;
    request.options.count = 6;
    request.input = RuntimeValue::literal(std::int64_t{7});
    request.seed = {19, {"root"}, 8, std::nullopt, std::nullopt};
    request.body = loop::make_function_body(body_request);
    const auto result = loop::run(request);
    return result.success && result.terminated_early
        && result.completed_iterations == 3 && result.accumulated.size() == 3
        && integer(result.accumulated[0]) == 100
        && integer(result.accumulated[2]) == 102
        && paths.size() == 3 && paths[0].back() == "loop:8:0"
        && paths[2].back() == "loop:8:2";
}

bool nested_failure_aborts_loop()
{
    using namespace phoenix;
    InstructionRegistry registry;
    registry.register_handler("loop-test-body", [](const auto& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        if (integer(*input(frame, "$index")) == 1)
            result.failure_message = "nested failure";
        else
            result.produced_outputs.push_back({"loop", *input(frame, "input")});
        return result;
    });
    const auto function = body_function();
    const FunctionExecutor executor{registry};
    loop::FunctionBodyRequest body_request;
    body_request.executor = &executor;
    body_request.function = &function;
    loop::RunRequest request;
    request.options.count = 3;
    request.input = RuntimeValue::literal(std::int64_t{7});
    request.body = loop::make_function_body(body_request);
    const auto result = loop::run(request);
    return !result.success && result.completed_iterations == 1
        && result.accumulated.empty() && result.error == "nested failure";
}

} // namespace

int main()
{
    const bool routing = nested_body_routes_feedback_index_and_precedence();
    const bool failure = nested_failure_aborts_loop();
    std::cout << "loop nested body routing and index: " << routing << '\n'
              << "loop nested body failure rollback: " << failure << '\n';
    return routing && failure ? 0 : 1;
}
