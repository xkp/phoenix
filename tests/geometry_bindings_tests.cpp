#include "phoenix/scripting/geometry_bindings.hpp"

#include <cmath>
#include <iostream>

namespace {

phoenix::CanonicalGeometryRef square_triangles()
{
    using namespace phoenix;
    std::vector<RuntimeVertex> vertices{
        {{0,0,0},VertexId{1},0},{{1,0,0},VertexId{2},1},
        {{1,1,0},VertexId{3},2},{{0,1,0},VertexId{4},4}};
    std::vector<RuntimeHalfedge> edges{
        {0,0,1,2,invalid_geometry_index,invalid_geometry_index,HalfedgeId{10},EdgeId{20},LabelId{30}},
        {1,0,2,0,invalid_geometry_index,invalid_geometry_index,HalfedgeId{11},EdgeId{21},LabelId{31}},
        {2,0,0,1,3,invalid_geometry_index,HalfedgeId{12},EdgeId{22},LabelId{32}},
        {0,1,4,5,2,invalid_geometry_index,HalfedgeId{13},EdgeId{22},LabelId{33}},
        {2,1,5,3,invalid_geometry_index,invalid_geometry_index,HalfedgeId{14},EdgeId{23},LabelId{34}},
        {3,1,3,4,invalid_geometry_index,invalid_geometry_index,HalfedgeId{15},EdgeId{24},LabelId{35}}};
    return CanonicalGeometry::create(std::move(vertices),std::move(edges),
        {{0,FaceId{40},LabelId{70}},{3,FaceId{41},LabelId{71}}});
}

double number(const phoenix::scripting::Bindings& values,const char* name)
{
    return std::get<double>(values.at(name));
}

bool evaluates_production_binding_family()
{
    using namespace phoenix::scripting;
    const auto geometry=square_triangles();
    GeometryBindingSpec distance{"distance",GeometryBindingKind::distance};distance.label1=phoenix::LabelId{30};distance.label2=phoenix::LabelId{31};distance.choice=BindingChoice::largest;
    GeometryBindingSpec extended{"extended",GeometryBindingKind::opposite_extended};extended.label1=phoenix::LabelId{32};extended.label2=phoenix::LabelId{70};extended.label3=phoenix::LabelId{33};extended.label4=phoenix::LabelId{71};
    std::vector<GeometryBindingSpec> specs{
        {"area",GeometryBindingKind::area},
        {"longest",GeometryBindingKind::length,{}, {},BindingChoice::largest},
        {"border",GeometryBindingKind::border_edge},
        {"edges",GeometryBindingKind::count_edge_labels},
        {"faces70",GeometryBindingKind::count_face_labels,phoenix::LabelId{70}},
        {"opposite",GeometryBindingKind::opposite_edge,phoenix::LabelId{32},phoenix::LabelId{33}},
        {"adjacent",GeometryBindingKind::adjacent,phoenix::LabelId{30},phoenix::LabelId{31},BindingChoice::any,BindingRelation::next},
        {"angle",GeometryBindingKind::angle,phoenix::LabelId{30},phoenix::LabelId{31},BindingChoice::any,BindingRelation::next},
        distance,extended};
    const auto values=evaluate_geometry_bindings(*geometry,BindingElementKind::face,0,specs,5);
    return std::abs(number(values,"area")-0.5)<1e-12
        &&std::abs(number(values,"longest")-std::sqrt(2.0))<1e-12
        &&number(values,"border")==1&&number(values,"edges")==3
        &&number(values,"faces70")==1&&number(values,"opposite")==1
        &&number(values,"adjacent")==1&&std::abs(number(values,"angle")-90)<1e-12
        &&std::abs(number(values,"distance")-std::sqrt(2.0))<1e-12
        &&number(values,"extended")==1;
}

bool preserves_and_validates_selection_identity()
{
    using namespace phoenix;using namespace phoenix::scripting;
    const auto geometry=square_triangles();
    ElementSelectionValue selection{{"selected",ActorId{"actor"},geometry},GeometryElementKind::face,{41}};
    const auto resolved=resolve_geometry_binding_input(RuntimeValue::element_selection(selection),BindingElementKind::face);
    selection.element_ids={999};
    const auto stale=resolve_geometry_binding_input(RuntimeValue::element_selection(selection),BindingElementKind::face);
    return resolved&&resolved->elements==std::vector<GeometryIndex>{1}&&!stale;
}

bool expands_geometry_collections()
{
    using namespace phoenix;using namespace phoenix::scripting;
    const auto geometry=square_triangles();
    GeometryCollectionValue collection{{{"a",ActorId{"a"},geometry},{"b",ActorId{"b"},geometry}}};
    const auto inputs=resolve_geometry_binding_inputs(RuntimeValue::geometry_collection(collection.contributions),BindingElementKind::face);
    return inputs.size()==2&&inputs[0].elements.size()==2&&inputs[1].source.accumulation_actor_id==ActorId{"b"};
}

} // namespace

int main()
{
    const bool values=evaluates_production_binding_family();
    const bool identity=preserves_and_validates_selection_identity();
    const bool collections=expands_geometry_collections();
    std::cout<<"geometry binding resolver family: "<<values<<'\n'
             <<"geometry binding stable selection identity: "<<identity<<'\n'
             <<"geometry binding collection expansion: "<<collections<<'\n';
    return values&&identity&&collections?0:1;
}
