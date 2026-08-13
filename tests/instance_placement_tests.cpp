#include "phoenix/instance/placement.hpp"
#include <cmath>
#include <iostream>

namespace {
phoenix::CanonicalGeometryRef tilted_quad()
{
    using namespace phoenix;
    return CanonicalGeometry::create({{{0,0,0},VertexId{1},0},{{2,0,0},VertexId{2},1},{{2,2,2},VertexId{3},2},{{0,2,2},VertexId{4},3}},
        {{0,0,1,3,invalid_geometry_index,0,HalfedgeId{10},EdgeId{20},LabelId{31}},
         {1,0,2,0,invalid_geometry_index,1,HalfedgeId{11},EdgeId{21},LabelId{32}},
         {2,0,3,1,invalid_geometry_index,2,HalfedgeId{12},EdgeId{22},LabelId{33}},
         {3,0,0,2,invalid_geometry_index,3,HalfedgeId{13},EdgeId{23},LabelId{34}}},{{0,FaceId{40},LabelId{70}}});
}
bool placement_and_label()
{
    phoenix::instance::PlacementOptions o;o.position=phoenix::instance::FacePosition::centroid;o.orientation_label=phoenix::LabelId{31};o.translation={1,2,0};
    const auto r=phoenix::instance::build_placements(*tilted_quad(),o);
    return r.success()&&r.placements.size()==1&&r.placements[0].source_face_id==phoenix::FaceId{40}
        &&std::abs(r.placements[0].origin.x-2.0)<1e-9
        &&std::abs(r.placements[0].origin.y-(1.0-std::sqrt(2.0)))<1e-9
        &&std::abs(r.placements[0].origin.z-(1.0+std::sqrt(2.0)))<1e-9;
}
bool missing_label_and_by_face_reject()
{
    phoenix::instance::PlacementOptions o;o.orientation_label=phoenix::LabelId{99};
    const auto missing=phoenix::instance::build_placements(*tilted_quad(),o);
    o.orientation_label.reset();o.orientation=phoenix::instance::OrientationMethod::by_face_unsupported;
    const auto by_face=phoenix::instance::build_placements(*tilted_quad(),o);
    return !missing.success()&&missing.placements.empty()&&!by_face.success();
}
}
int main(){const bool a=placement_and_label(),b=missing_label_and_by_face_reject();std::cout<<"instance 3D placement and orientation label: "<<a<<'\n'<<"instance invalid modes transactional: "<<b<<'\n';return a&&b?0:1;}
