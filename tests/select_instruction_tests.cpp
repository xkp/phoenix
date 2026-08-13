#include "phoenix/select/instruction.hpp"

#include <cstdlib>
#include <iostream>
#include <set>

namespace {

phoenix::CanonicalGeometryRef two_triangles()
{
    using namespace phoenix;
    std::vector<RuntimeVertex> vertices{
        {{0,0,0}, VertexId{1}, 0}, {{1,0,0}, VertexId{2}, 1},
        {{1,1,0}, VertexId{3}, 2}, {{0,1,0}, VertexId{4}, 4}};
    std::vector<RuntimeHalfedge> edges{
        {0,0,1,2,invalid_geometry_index,invalid_geometry_index,HalfedgeId{10},EdgeId{20},LabelId{30}},
        {1,0,2,0,invalid_geometry_index,invalid_geometry_index,HalfedgeId{11},EdgeId{21},LabelId{31}},
        {2,0,0,1,3,invalid_geometry_index,HalfedgeId{12},EdgeId{22},LabelId{32}},
        {0,1,4,5,2,invalid_geometry_index,HalfedgeId{13},EdgeId{22},LabelId{33}},
        {2,1,5,3,invalid_geometry_index,invalid_geometry_index,HalfedgeId{14},EdgeId{23},LabelId{34}},
        {3,1,3,4,invalid_geometry_index,invalid_geometry_index,HalfedgeId{15},EdgeId{24},LabelId{35}}};
    return CanonicalGeometry::create(std::move(vertices), std::move(edges),
        {{0, FaceId{40}, LabelId{70}}, {3, FaceId{41}, LabelId{71}}});
}

const phoenix::GeometryCollectionValue* output(
    const phoenix::InstructionResult& result, const phoenix::PortId& port)
{
    for (const auto& item : result.produced_outputs)
        if (item.port == port) return item.value.as_geometry_collection();
    return nullptr;
}

} // namespace

int main()
{
    using namespace phoenix;
    const auto source = two_triangles();
    InstructionExecutionFrame frame;
    frame.inputs.node_id = 1;
    frame.inputs.promised_inputs = {{"geometry", RuntimeValue::geometry(
        source, "source", ActorId{"actor:select"})}};
    frame.effective_seed = 17;

    select::InstructionConfig routes;
    routes.label_routes = {{LabelId{70}, "a"}, {LabelId{71}, "b"}};
    const auto routed = select::make_instruction_handler(routes)(frame);
    const auto* a = output(routed, "a");
    const auto* b = output(routed, "b");
    const bool label_routing = a && b && a->contributions.size() == 1
        && b->contributions.size() == 1
        && a->contributions[0].geometry->faces()[0].id == FaceId{40}
        && b->contributions[0].geometry->faces()[0].id == FaceId{41};

    select::InstructionConfig conditions;
    select::FaceCondition condition;
    condition.edge_label = LabelId{30};
    condition.require_border_edge = true;
    condition.minimum_edge_length = 0.5;
    condition.maximum_edge_length = 1.5;
    conditions.conditions = {condition};
    const auto conditioned = select::make_instruction_handler(conditions)(frame);
    const auto* selected = output(conditioned, "output");
    const auto* otherwise = output(conditioned, "else");
    const bool predicates = selected && otherwise
        && selected->contributions.size() == 1
        && otherwise->contributions.size() == 1
        && selected->contributions[0].geometry->faces()[0].id == FaceId{40};

    select::InstructionConfig limited;
    limited.limit.count = 50;
    limited.limit.percentage = true;
    limited.limit.seed = 99;
    const auto first_limit = select::make_instruction_handler(limited)(frame);
    const auto second_limit = select::make_instruction_handler(limited)(frame);
    const auto* first_selected = output(first_limit, "output");
    const auto* first_else = output(first_limit, "else");
    const auto* second_selected = output(second_limit, "output");
    const bool deterministic_limit = first_selected && first_else && second_selected
        && first_selected->contributions.size() == 1
        && first_else->contributions.size() == 1
        && first_selected->contributions[0].geometry->faces()[0].id
            == second_selected->contributions[0].geometry->faces()[0].id;

    const bool immutable = routed.geometry_effects.empty()
        && source->faces().size() == 2
        && source->faces()[0].label == LabelId{70}
        && source->faces()[1].label == LabelId{71};

    std::cout << "select label routing: " << label_routing << '\n'
              << "select face predicates: " << predicates << '\n'
              << "select deterministic percentage limit: " << deterministic_limit << '\n'
              << "select immutable non-consuming behavior: " << immutable << '\n';
    return label_routing && predicates && deterministic_limit && immutable
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
