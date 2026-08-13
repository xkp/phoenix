#include "phoenix/partition/working_face.hpp"

#include <CGAL/number_utils.h>

#include <cmath>

namespace phoenix::partition {
namespace {

Point3d subtract(Point3d left, Point3d right)
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

double dot(Point3d left, Point3d right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Point3d cross(Point3d left, Point3d right)
{
    return {left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
}

double norm(Point3d value)
{
    return std::sqrt(dot(value, value));
}

Point3d normalized(Point3d value)
{
    const auto length = norm(value);
    return {value.x / length, value.y / length, value.z / length};
}

} // namespace

Point3d PlanarFrame::lift(const ExactPoint2& point) const
{
    const auto u = CGAL::to_double(point.x());
    const auto v = CGAL::to_double(point.y());
    return {origin.x + axis_u.x * u + axis_v.x * v,
        origin.y + axis_u.y * u + axis_v.y * v,
        origin.z + axis_u.z * u + axis_v.z * v};
}

ProjectionResult ExactFaceProjector::project(
    const CanonicalGeometry& geometry,
    GeometryIndex face_index) const
{
    ProjectionResult result;
    if (face_index >= geometry.faces().size()) {
        result.error = "Partition source face index is invalid.";
        return result;
    }
    const auto& source_face = geometry.faces()[face_index];
    std::vector<GeometryIndex> loop;
    auto halfedge_index = source_face.halfedge;
    do {
        if (halfedge_index >= geometry.halfedges().size()) {
            result.error = "Partition source face has a broken boundary.";
            return result;
        }
        loop.push_back(halfedge_index);
        halfedge_index = geometry.halfedges()[halfedge_index].next;
    } while (halfedge_index != source_face.halfedge && loop.size() <= geometry.halfedges().size());
    if (loop.size() < 3 || halfedge_index != source_face.halfedge) {
        result.error = "Partition source face requires one closed boundary of at least three edges.";
        return result;
    }

    const auto origin = geometry.vertices()[geometry.halfedges()[loop[0]].origin_vertex].point;
    Point3d normal{};
    for (std::size_t i = 1; i + 1 < loop.size(); ++i) {
        const auto first = geometry.vertices()[geometry.halfedges()[loop[i]].origin_vertex].point;
        const auto second = geometry.vertices()[geometry.halfedges()[loop[i + 1]].origin_vertex].point;
        normal = cross(subtract(first, origin), subtract(second, origin));
        if (norm(normal) > 1e-12) break;
    }
    if (norm(normal) <= 1e-12) {
        result.error = "Partition source face is degenerate.";
        return result;
    }
    normal = normalized(normal);
    Point3d axis_u{};
    for (std::size_t i = 1; i < loop.size(); ++i) {
        const auto point = geometry.vertices()[geometry.halfedges()[loop[i]].origin_vertex].point;
        axis_u = subtract(point, origin);
        if (norm(axis_u) > 1e-12) break;
    }
    axis_u = normalized(axis_u);
    const auto axis_v = cross(normal, axis_u);
    result.face.source_face_id = source_face.id;
    result.face.source_face_label = source_face.label;
    result.face.frame = {origin, axis_u, axis_v, normal};
    for (const auto index : loop) {
        const auto& halfedge = geometry.halfedges()[index];
        const auto point = geometry.vertices()[halfedge.origin_vertex].point;
        const auto offset = subtract(point, origin);
        if (std::abs(dot(offset, normal)) > 1e-8) {
            result.error = "Partition source face is not planar.";
            result.face.boundary.clear();
            return result;
        }
        LabelId opposite = unassigned_label_id;
        LabelId opposite_face = unassigned_label_id;
        std::optional<HalfedgeId> opposite_halfedge_id;
        if (halfedge.opposite != invalid_geometry_index) {
            const auto& opposite_halfedge = geometry.halfedges()[halfedge.opposite];
            opposite = opposite_halfedge.label;
            opposite_halfedge_id = opposite_halfedge.id;
            if (opposite_halfedge.face != invalid_geometry_index)
                opposite_face = geometry.faces()[opposite_halfedge.face].label;
        }
        result.face.boundary.push_back({
            ExactPoint2{dot(offset, axis_u), dot(offset, axis_v)},
            geometry.vertices()[halfedge.origin_vertex].id,
            halfedge.id,
            halfedge.edge_id,
            halfedge.label,
            opposite,
            opposite_face,
            opposite_halfedge_id});
    }
    return result;
}

} // namespace phoenix::partition
