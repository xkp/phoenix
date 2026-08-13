#pragma once

#include "phoenix/geometry.hpp"

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>

#include <optional>
#include <string>
#include <vector>

namespace phoenix::partition {

using ExactKernel = CGAL::Exact_predicates_exact_constructions_kernel;
using ExactPoint2 = ExactKernel::Point_2;

struct WorkingBoundaryPoint {
    ExactPoint2 point;
    VertexId source_vertex_id;
    HalfedgeId source_halfedge_id;
    EdgeId source_edge_id;
    LabelId current_label = unassigned_label_id;
    LabelId opposite_label = unassigned_label_id;
    LabelId opposite_face_label = unassigned_label_id;
    std::optional<HalfedgeId> source_opposite_halfedge_id;
};

struct PlanarFrame {
    Point3d origin;
    Point3d axis_u;
    Point3d axis_v;
    Point3d normal;

    [[nodiscard]] Point3d lift(const ExactPoint2& point) const;
};

struct ExactWorkingFace {
    FaceId source_face_id;
    LabelId source_face_label = unassigned_label_id;
    PlanarFrame frame;
    std::vector<WorkingBoundaryPoint> boundary;
};

struct ProjectionResult {
    ExactWorkingFace face;
    std::string error;
    [[nodiscard]] bool success() const noexcept { return error.empty(); }
};

class ExactFaceProjector {
public:
    [[nodiscard]] ProjectionResult project(
        const CanonicalGeometry& geometry,
        GeometryIndex face_index) const;
};

} // namespace phoenix::partition
