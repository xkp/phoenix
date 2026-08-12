#include "phoenix/extrusion/instruction.hpp"

#include <cstdlib>
#include <iostream>

namespace {

phoenix::CanonicalGeometryRef source_triangle()
{
    std::vector<phoenix::RuntimeVertex> vertices{
        {{0, 0, 0}, phoenix::VertexId{100}, 0},
        {{1, 0, 0}, phoenix::VertexId{101}, 1},
        {{0, 1, 0}, phoenix::VertexId{102}, 2},
    };
    std::vector<phoenix::RuntimeHalfedge> halfedges{
        {0, 0, 1, 2, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{110}, phoenix::EdgeId{120}, phoenix::LabelId{51}},
        {1, 0, 2, 0, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{111}, phoenix::EdgeId{121}, phoenix::LabelId{52}},
        {2, 0, 0, 1, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{112}, phoenix::EdgeId{122}, phoenix::LabelId{53}},
    };
    return phoenix::CanonicalGeometry::create(
        std::move(vertices), std::move(halfedges),
        {{0, phoenix::FaceId{130}, phoenix::LabelId{60}}});
}

phoenix::CanonicalGeometryRef source_two_triangles(bool degenerate_second = false)
{
    std::vector<phoenix::RuntimeVertex> vertices{
        {{0, 0, 0}, phoenix::VertexId{100}, 0},
        {{1, 0, 0}, phoenix::VertexId{101}, 1},
        {{0, 1, 0}, phoenix::VertexId{102}, 2},
        {{3, 0, 0}, phoenix::VertexId{200}, 3},
        {{4, 0, 0}, phoenix::VertexId{201}, 4},
        {{degenerate_second ? 5.0 : 3.0, degenerate_second ? 0.0 : 1.0, 0},
            phoenix::VertexId{202}, 5},
    };
    std::vector<phoenix::RuntimeHalfedge> halfedges{
        {0, 0, 1, 2, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{110}, phoenix::EdgeId{120}, phoenix::LabelId{51}},
        {1, 0, 2, 0, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{111}, phoenix::EdgeId{121}, phoenix::LabelId{52}},
        {2, 0, 0, 1, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{112}, phoenix::EdgeId{122}, phoenix::LabelId{53}},
        {3, 1, 4, 5, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{210}, phoenix::EdgeId{220}, phoenix::LabelId{54}},
        {4, 1, 5, 3, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{211}, phoenix::EdgeId{221}, phoenix::LabelId{55}},
        {5, 1, 3, 4, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{212}, phoenix::EdgeId{222}, phoenix::LabelId{56}},
    };
    return phoenix::CanonicalGeometry::create(std::move(vertices), std::move(halfedges), {
        {0, phoenix::FaceId{130}, phoenix::LabelId{60}},
        {3, phoenix::FaceId{230}, phoenix::LabelId{61}},
    });
}

phoenix::FunctionDescriptor extrusion_function()
{
    using phoenix::PortDescriptor;
    using phoenix::PortDirection;
    phoenix::FunctionDescriptor function;
    function.id = "extrusion-integration";
    function.input_ports = {{"geometry", "geometry", PortDirection::input}};
    function.output_ports = {{"result", "geometry", PortDirection::output}};
    phoenix::InstructionDescriptor extrusion;
    extrusion.id = 1;
    extrusion.kind = "extrusion";
    extrusion.input_ports = {{"geometry", "geometry", PortDirection::input}};
    extrusion.output_ports = {{"result", "geometry", PortDirection::output}};
    extrusion.has_else_port = false;
    extrusion.consumes_geometry = true;
    phoenix::InstructionDescriptor output;
    output.id = 99;
    output.kind = "output";
    output.input_ports = {{"result", "geometry", PortDirection::input}};
    output.has_else_port = false;
    function.instructions = {extrusion, output};
    function.edges = {{1, "result", 99, "result"}};
    function.output_node_id = 99;
    return function;
}

phoenix::FunctionDescriptor extrusion_failure_function()
{
    using phoenix::PortDirection;
    phoenix::FunctionDescriptor function;
    function.id = "extrusion-failure";
    function.input_ports = {{"geometry", "geometry", PortDirection::input}};
    function.output_ports = {
        {"success", "geometry", PortDirection::output},
        {"fallback", "geometry", PortDirection::output},
    };
    phoenix::InstructionDescriptor extrusion;
    extrusion.id = 1;
    extrusion.kind = "extrusion";
    extrusion.input_ports = {{"geometry", "geometry", PortDirection::input}};
    extrusion.output_ports = {
        {"result", "geometry", PortDirection::output},
        {"else", "geometry", PortDirection::output},
    };
    extrusion.multiplexes_input = true;
    extrusion.consumes_geometry = true;
    phoenix::InstructionDescriptor fallback;
    fallback.id = 2;
    fallback.kind = "fallback";
    fallback.input_ports = {{"geometry", "geometry", PortDirection::input}};
    fallback.output_ports = {{"result", "geometry", PortDirection::output}};
    fallback.has_else_port = false;
    phoenix::InstructionDescriptor output;
    output.id = 99;
    output.kind = "output";
    output.input_ports = {
        {"success", "geometry", PortDirection::input},
        {"fallback", "geometry", PortDirection::input},
    };
    output.has_else_port = false;
    function.instructions = {extrusion, fallback, output};
    function.edges = {
        {1, "result", 99, "success"},
        {1, "else", 2, "geometry"},
        {2, "result", 99, "fallback"},
    };
    function.output_node_id = 99;
    return function;
}

phoenix::FunctionDescriptor extrusion_branches_function()
{
    using phoenix::PortDirection;
    phoenix::FunctionDescriptor function;
    function.id = "extrusion-branches";
    function.input_ports = {{"geometry", "geometry", PortDirection::input}};
    function.output_ports = {
        {"left", "geometry", PortDirection::output},
        {"right", "geometry", PortDirection::output},
    };
    auto branch = [](phoenix::NodeId id) {
        phoenix::InstructionDescriptor instruction;
        instruction.id = id;
        instruction.kind = "extrusion";
        instruction.input_ports = {{"geometry", "geometry", PortDirection::input}};
        instruction.output_ports = {{"result", "geometry", PortDirection::output}};
        instruction.has_else_port = false;
        instruction.consumes_geometry = true;
        return instruction;
    };
    phoenix::InstructionDescriptor output;
    output.id = 99;
    output.kind = "output";
    output.input_ports = {
        {"left", "geometry", PortDirection::input},
        {"right", "geometry", PortDirection::input},
    };
    output.has_else_port = false;
    function.instructions = {branch(1), branch(2), output};
    function.edges = {
        {1, "result", 99, "left"},
        {2, "result", 99, "right"},
    };
    function.output_node_id = 99;
    return function;
}

bool test_executor_consumes_and_publishes_replacement()
{
    phoenix::extrusion::ProfileSegment segment;
    segment.delta_y = 1;
    segment.face_label = phoenix::LabelId{70};
    segment.left_label = phoenix::LabelId{71};
    segment.bottom_label = phoenix::LabelId{72};
    segment.right_label = phoenix::LabelId{73};
    segment.top_label = phoenix::LabelId{74};
    segment.skirt_label = phoenix::LabelId{75};

    phoenix::extrusion::InstructionConfig config;
    config.profile = phoenix::extrusion::Profile::create({segment});
    config.bottom_label = phoenix::LabelId{80};
    config.right_label = phoenix::LabelId{81};
    config.top_label = phoenix::LabelId{82};
    config.left_label = phoenix::LabelId{83};
    config.skirt_label = phoenix::LabelId{84};
    config.cap_label = phoenix::LabelId{85};

    phoenix::InstructionRegistry registry;
    registry.register_handler("extrusion", phoenix::extrusion::make_instruction_handler(config));
    const auto function = extrusion_function();
    const auto source = source_triangle();
    phoenix::GeometryPublicationLedger ledger;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(source, "source", "actor:root")}};
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.publication_ledger = &ledger;

    const auto result = phoenix::FunctionExecutor{registry}.run(request);
    const auto final = ledger.assemble_actor("actor:root");
    if (result.status != phoenix::FunctionExecutionStatus::completed
        || !result.failures.empty() || !final || final->faces().size() != 4
        || !ledger.is_consumed({"actor:root", phoenix::FaceId{130}})) return false;
    for (const auto& face : final->faces())
        if (face.id == phoenix::FaceId{130}) return false;
    for (const auto& vertex : final->vertices())
        if (vertex.id.valid() && vertex.id.value() > 102 && vertex.id.value() <= 130) return false;
    return true;
}

bool test_multiple_faces_are_independent_items()
{
    phoenix::extrusion::ProfileSegment segment;
    segment.delta_y = 1;
    segment.face_label = phoenix::LabelId{70};
    segment.left_label = phoenix::LabelId{71};
    segment.bottom_label = phoenix::LabelId{72};
    segment.right_label = phoenix::LabelId{73};
    segment.top_label = phoenix::LabelId{74};
    segment.skirt_label = phoenix::LabelId{75};
    phoenix::extrusion::InstructionConfig config;
    config.profile = phoenix::extrusion::Profile::create({segment});
    config.cap_label = phoenix::LabelId{85};

    phoenix::InstructionRegistry registry;
    registry.register_handler("extrusion", phoenix::extrusion::make_instruction_handler(config));
    const auto function = extrusion_function();
    const auto source = source_two_triangles();
    phoenix::GeometryPublicationLedger ledger;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(
        source, "source", "actor:multi")}};
    request.context.function_id = function.id;
    request.context.call_path = {"multi"};
    request.publication_ledger = &ledger;

    const auto result = phoenix::FunctionExecutor{registry}.run(request);
    const auto final = ledger.assemble_actor("actor:multi");
    const auto* output = result.outputs.empty()
        ? nullptr : result.outputs.front().value.as_geometry_collection();
    phoenix::GeometryPublicationLedger threaded_ledger;
    auto threaded_request = request;
    threaded_request.publication_ledger = &threaded_ledger;
    threaded_request.options.worker_count = 4;
    const auto threaded_result = phoenix::FunctionExecutor{registry}.run(threaded_request);
    const auto threaded_final = threaded_ledger.assemble_actor("actor:multi");
    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.failures.empty() && output && output->contributions.size() == 2
        && final && final->faces().size() == 8
        && ledger.is_consumed({"actor:multi", phoenix::FaceId{130}})
        && ledger.is_consumed({"actor:multi", phoenix::FaceId{230}})
        && threaded_result.status == phoenix::FunctionExecutionStatus::completed
        && threaded_result.failures.empty() && threaded_final
        && final->fingerprint() == threaded_final->fingerprint();
}

bool test_failed_face_routes_alone_and_is_not_consumed()
{
    phoenix::extrusion::ProfileSegment segment;
    segment.delta_y = 1;
    segment.face_label = phoenix::LabelId{70};
    phoenix::extrusion::InstructionConfig config;
    config.profile = phoenix::extrusion::Profile::create({segment});
    config.cap_label = phoenix::LabelId{85};

    bool fallback_saw_only_failed_face = false;
    phoenix::InstructionRegistry registry;
    registry.register_handler("extrusion", phoenix::extrusion::make_instruction_handler(config));
    registry.register_handler("fallback",
        [&fallback_saw_only_failed_face](const phoenix::InstructionExecutionFrame& frame) {
            const phoenix::GeometryValue* geometry = nullptr;
            for (const auto& input : frame.inputs.promised_inputs)
                if (input.port == "geometry") geometry = input.value.as_geometry();
            fallback_saw_only_failed_face = geometry && geometry->geometry
                && geometry->geometry->faces().size() == 1
                && geometry->geometry->faces()[0].id == phoenix::FaceId{230};
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {{"result", phoenix::RuntimeValue::geometry("fallback")}},
                std::nullopt};
        });
    const auto function = extrusion_failure_function();
    const auto source = source_two_triangles(true);
    phoenix::GeometryPublicationLedger ledger;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(
        source, "source", "actor:failure")}};
    request.context.function_id = function.id;
    request.context.call_path = {"failure"};
    request.publication_ledger = &ledger;

    const auto result = phoenix::FunctionExecutor{registry}.run(request);
    const auto final = ledger.assemble_actor("actor:failure");
    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.failures.size() == 1 && result.failures[0].item_key == 1
        && fallback_saw_only_failed_face
        && ledger.is_consumed({"actor:failure", phoenix::FaceId{130}})
        && !ledger.is_consumed({"actor:failure", phoenix::FaceId{230}})
        && final && final->faces().size() == 5;
}

bool test_two_consuming_branches_keep_both_replacements()
{
    phoenix::extrusion::ProfileSegment segment;
    segment.delta_y = 1;
    segment.face_label = phoenix::LabelId{70};
    phoenix::extrusion::InstructionConfig config;
    config.profile = phoenix::extrusion::Profile::create({segment});
    config.cap_label = phoenix::LabelId{85};
    phoenix::InstructionRegistry registry;
    registry.register_handler("extrusion", phoenix::extrusion::make_instruction_handler(config));
    const auto function = extrusion_branches_function();
    phoenix::GeometryPublicationLedger ledger;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(
        source_triangle(), "source", "actor:branches")}};
    request.context.function_id = function.id;
    request.context.call_path = {"branches"};
    request.options.worker_count = 4;
    request.publication_ledger = &ledger;

    const auto result = phoenix::FunctionExecutor{registry}.run(request);
    const auto final = ledger.assemble_actor("actor:branches");
    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.failures.empty() && final && final->faces().size() == 8
        && ledger.is_consumed({"actor:branches", phoenix::FaceId{130}});
}

} // namespace

int main()
{
    const bool publication = test_executor_consumes_and_publishes_replacement();
    const bool multiple = test_multiple_faces_are_independent_items();
    const bool failure = test_failed_face_routes_alone_and_is_not_consumed();
    const bool branches = test_two_consuming_branches_keep_both_replacements();
    const bool ok = publication && multiple && failure && branches;
    std::cout << "extrusion execution/publication: " << publication << '\n'
              << "multi-face items: " << multiple << '\n'
              << "per-face failure/else: " << failure << '\n'
              << "two consuming branches: " << branches << '\n';
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
