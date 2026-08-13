#include "phoenix/rename/instruction.hpp"

#include <cstdlib>
#include <iostream>

namespace {
phoenix::CanonicalGeometryRef square()
{
    using namespace phoenix;
    std::vector<RuntimeVertex> vertices{
        {{0,0,0},VertexId{1},0}, {{2,0,0},VertexId{2},1},
        {{2,1,0},VertexId{3},2}, {{0,1,0},VertexId{4},4}};
    std::vector<RuntimeHalfedge> edges{
        {0,0,1,2,invalid_geometry_index,invalid_geometry_index,HalfedgeId{10},EdgeId{20},LabelId{30}},
        {1,0,2,0,invalid_geometry_index,invalid_geometry_index,HalfedgeId{11},EdgeId{21},LabelId{31}},
        {2,0,0,1,3,invalid_geometry_index,HalfedgeId{12},EdgeId{22},LabelId{32}},
        {0,1,4,5,2,invalid_geometry_index,HalfedgeId{13},EdgeId{22},LabelId{33}},
        {2,1,5,3,invalid_geometry_index,invalid_geometry_index,HalfedgeId{14},EdgeId{23},LabelId{34}},
        {3,1,3,4,invalid_geometry_index,invalid_geometry_index,HalfedgeId{15},EdgeId{24},LabelId{35}}};
    return CanonicalGeometry::create(std::move(vertices), std::move(edges),
        {{0,FaceId{40},LabelId{70}}, {3,FaceId{41},LabelId{71}}});
}

phoenix::CanonicalGeometryRef run(const phoenix::rename::InstructionConfig& config,
    phoenix::CanonicalGeometryRef source)
{
    phoenix::InstructionExecutionFrame frame;
    frame.inputs.node_id = 1;
    frame.inputs.promised_inputs = {{"geometry", phoenix::RuntimeValue::geometry(source)}};
    frame.effective_seed = 7;
    const auto result = phoenix::rename::make_instruction_handler(config)(frame);
    const auto* collection = result.produced_outputs.front().value.as_geometry_collection();
    return collection && !collection->contributions.empty()
        ? collection->contributions.front().geometry : nullptr;
}
}

int main()
{
    using namespace phoenix;
    const auto source = square();
    rename::InstructionConfig manual;
    manual.label_map[LabelId{70}] = {LabelId{80}};
    manual.label_map[LabelId{30}] = {LabelId{90}};
    const auto mapped = run(manual, source);
    const bool manual_map = mapped && mapped->faces()[0].label == LabelId{80}
        && mapped->halfedges()[0].label == LabelId{90};

    rename::InstructionConfig longest;
    rename::Condition edge_condition;
    edge_condition.target = rename::Target::directed_edges;
    edge_condition.to_label = LabelId{100};
    edge_condition.length_kind = rename::LengthKind::largest;
    longest.conditions = {edge_condition};
    const auto length_result = run(longest, source);
    const bool largest = length_result
        && length_result->halfedges()[2].label == LabelId{100}
        && length_result->halfedges()[3].label == LabelId{100}
        && length_result->halfedges()[0].label == LabelId{30};

    rename::InstructionConfig opposite;
    rename::Condition opposite_condition;
    opposite_condition.target = rename::Target::directed_edges;
    opposite_condition.from_label = LabelId{32};
    opposite_condition.opposite_edge_label = LabelId{33};
    opposite_condition.to_owning_face_label = true;
    opposite.conditions = {opposite_condition};
    const auto opposite_result = run(opposite, source);
    const bool opposite_labels = opposite_result
        && opposite_result->halfedges()[2].label == LabelId{70}
        && opposite_result->halfedges()[3].label == LabelId{33};

    rename::InstructionConfig edge_count;
    rename::Condition face_condition;
    face_condition.target = rename::Target::faces;
    face_condition.maximum_edge_count = 3;
    face_condition.to_label = LabelId{110};
    edge_count.conditions = {face_condition};
    const auto face_result = run(edge_count, source);
    const bool face_conditions = face_result
        && face_result->faces()[0].label == LabelId{110}
        && face_result->faces()[1].label == LabelId{110};

    bool ids_preserved = mapped && mapped->vertices().size() == source->vertices().size();
    for (GeometryIndex i=0; ids_preserved && i<source->vertices().size(); ++i)
        ids_preserved = mapped->vertices()[i].id == source->vertices()[i].id;
    for (GeometryIndex i=0; ids_preserved && i<source->halfedges().size(); ++i)
        ids_preserved = mapped->halfedges()[i].id == source->halfedges()[i].id
            && mapped->halfedges()[i].edge_id == source->halfedges()[i].edge_id;
    for (GeometryIndex i=0; ids_preserved && i<source->faces().size(); ++i)
        ids_preserved = mapped->faces()[i].id == source->faces()[i].id;
    const bool immutable = source->faces()[0].label == LabelId{70}
        && source->halfedges()[0].label == LabelId{30};

    std::cout << "rename manual maps: " << manual_map << '\n'
              << "rename largest directed edge: " << largest << '\n'
              << "rename opposite labels/from face: " << opposite_labels << '\n'
              << "rename face edge-count condition: " << face_conditions << '\n'
              << "rename IDs preserved and source immutable: "
              << (ids_preserved && immutable) << '\n';
    return manual_map && largest && opposite_labels && face_conditions
        && ids_preserved && immutable ? EXIT_SUCCESS : EXIT_FAILURE;
}
