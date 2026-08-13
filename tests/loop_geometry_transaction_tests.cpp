#include "phoenix/loop/geometry_transaction.hpp"

#include <iostream>

namespace {

phoenix::CanonicalGeometryRef triangle(phoenix::FaceId id, double offset)
{
    using namespace phoenix;
    return CanonicalGeometry::create({
        {{offset, 0, 0}, VertexId{1}, 0}, {{offset + 1, 0, 0}, VertexId{2}, 1},
        {{offset, 1, 0}, VertexId{3}, 2}}, {
        {0, 0, 1, 2, invalid_geometry_index, 0, HalfedgeId{10}, EdgeId{20}, {}},
        {1, 0, 2, 0, invalid_geometry_index, 1, HalfedgeId{11}, EdgeId{21}, {}},
        {2, 0, 0, 1, invalid_geometry_index, 2, HalfedgeId{12}, EdgeId{22}, {}}},
        {{0, id, LabelId{70}}});
}

phoenix::FunctionDescriptor body_function()
{
    using namespace phoenix;
    FunctionDescriptor function;
    function.id = "loop-geometry-body";
    function.input_ports = {{"input", "geometry", PortDirection::input},
        {"$index", "integer", PortDirection::input}};
    function.output_ports = {{"loop", "geometry", PortDirection::output},
        {"output", "geometry", PortDirection::output}};
    InstructionDescriptor body;
    body.id = 1; body.kind = "geometry-body";
    body.input_ports = function.input_ports; body.output_ports = function.output_ports;
    body.consumes_geometry = true;
    InstructionDescriptor output;
    output.id = 99; output.kind = "output"; output.input_ports = function.output_ports;
    function.instructions = {body, output};
    function.edges = {{1, "loop", 99, "loop"}, {1, "output", 99, "output"}};
    function.output_node_id = 99;
    return function;
}

const phoenix::RuntimeValue* input(const phoenix::InstructionExecutionFrame& frame,
    const phoenix::PortId& port)
{
    for (const auto& value : frame.inputs.promised_inputs)
        if (value.port == port) return &value.value;
    return nullptr;
}

std::int64_t integer(const phoenix::RuntimeValue& value)
{
    const auto* literal = value.as_literal();
    const auto scalar = literal ? phoenix::literal_first_scalar(*literal) : std::nullopt;
    const auto* result = scalar ? std::get_if<std::int64_t>(&*scalar) : nullptr;
    return result ? *result : -1;
}

phoenix::loop::GeometryTransactionResult execute(bool fail_second)
{
    using namespace phoenix;
    InstructionRegistry registry;
    registry.register_handler("geometry-body", [fail_second](const auto& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        const auto index = integer(*input(frame, "$index"));
        if (fail_second && index == 1) {
            result.failure_message = "iteration failed";
            return result;
        }
        const auto generated = triangle(FaceId{static_cast<std::uint64_t>(100 + index)},
            static_cast<double>(index + 1));
        const auto* source = input(frame, "input")->as_geometry();
        GeometryItemEffect effect;
        effect.generated_geometry = generated;
        effect.consumed_faces.push_back({"actor:loop", source->geometry->faces()[0].id});
        result.geometry_effects.push_back(std::move(effect));
        result.produced_outputs.push_back({"output", RuntimeValue::geometry(
            generated, "iteration", ActorId{"actor:loop"})});
        if (index < 2) result.produced_outputs.push_back({"loop", RuntimeValue::geometry(
            generated, "feedback", ActorId{"actor:loop"})});
        return result;
    });
    const auto function = body_function();
    const FunctionExecutor executor{registry};
    loop::GeometryTransactionRequest request;
    request.options.count = 3;
    request.source = {"source", ActorId{"actor:loop"}, triangle(FaceId{40}, 0)};
    request.owner_actor_id = "actor:loop";
    request.seed = {9, {"root"}, 8, std::nullopt, std::nullopt};
    request.body.executor = &executor;
    request.body.function = &function;
    request.body.loop_node_id = 8;
    request.body.execution.context.call_path = {"root"};
    request.item_key = 12;
    return loop::run_geometry_transaction(request);
}

bool success_collapses_to_one_outer_effect()
{
    const auto result = execute(false);
    return result.loop.success && result.loop.completed_iterations == 3
        && result.publication.succeeded && result.publication.item_key == 12
        && result.publication.consumed_faces.size() == 1
        && result.publication.consumed_faces[0].face_id == phoenix::FaceId{40}
        && result.publication.generated_geometry
        && result.publication.generated_geometry->faces().size() == 3;
}

bool failure_exposes_nothing()
{
    const auto result = execute(true);
    return !result.loop.success && !result.publication.succeeded
        && !result.publication.generated_geometry
        && result.publication.consumed_faces.empty();
}

} // namespace

int main()
{
    const bool success = success_collapses_to_one_outer_effect();
    const bool failure = failure_exposes_nothing();
    std::cout << "loop atomic geometry collapse: " << success << '\n'
              << "loop geometry failure rollback: " << failure << '\n';
    return success && failure ? 0 : 1;
}
