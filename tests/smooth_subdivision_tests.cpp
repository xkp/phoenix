#include "phoenix/smooth/subdivision.hpp"

#include <cmath>
#include <iostream>

namespace {

phoenix::CanonicalGeometryRef quad()
{
    using namespace phoenix;
    return CanonicalGeometry::create({
        {{0, 0, 0}, VertexId{1}, 0}, {{2, 0, 0}, VertexId{2}, 1},
        {{2, 2, 0}, VertexId{3}, 2}, {{0, 2, 0}, VertexId{4}, 3}}, {
        {0, 0, 1, 3, invalid_geometry_index, 0, HalfedgeId{10}, EdgeId{20}, LabelId{31}},
        {1, 0, 2, 0, invalid_geometry_index, 1, HalfedgeId{11}, EdgeId{21}, LabelId{32}},
        {2, 0, 3, 1, invalid_geometry_index, 2, HalfedgeId{12}, EdgeId{22}, LabelId{33}},
        {3, 0, 0, 2, invalid_geometry_index, 3, HalfedgeId{13}, EdgeId{23}, LabelId{34}}},
        {{0, FaceId{40}, LabelId{77}}});
}

bool bilinear_quad_refines_and_inherits_label()
{
    phoenix::RunElementIdAllocator ids(1000);
    phoenix::smooth::SubdivisionOptions options;
    options.max_refinement_level = 1;
    const auto result = phoenix::smooth::subdivide(*quad(), ids, options);
    if (!result.success() || result.geometry->faces().size() != 4
        || result.geometry->vertices().size() != 9) return false;
    for (const auto& face : result.geometry->faces())
        if (face.label != phoenix::LabelId{77} || face.id.value() < 1000) return false;
    for (const auto& halfedge : result.geometry->halfedges())
        if (halfedge.label != phoenix::unassigned_label_id) return false;
    return true;
}

bool zero_level_is_identity_copy()
{
    phoenix::RunElementIdAllocator ids(1000);
    phoenix::smooth::SubdivisionOptions options;
    options.max_refinement_level = 0;
    const auto source = quad();
    const auto result = phoenix::smooth::subdivide(*source, ids, options);
    return result.success() && result.geometry->serialize_canonical() == source->serialize_canonical();
}

} // namespace

int main()
{
    const bool refine = bilinear_quad_refines_and_inherits_label();
    const bool identity = zero_level_is_identity_copy();
    std::cout << "smooth bilinear topology and labels: " << refine << '\n'
              << "smooth zero-level identity: " << identity << '\n';
    return refine && identity ? 0 : 1;
}
