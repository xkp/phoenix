#pragma once

#include "phoenix/working_geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace phoenix {

using WorkingVertexIndex = std::size_t;
using WorkingFaceIndex = std::size_t;
inline constexpr WorkingFaceIndex invalid_working_face_index =
    static_cast<WorkingFaceIndex>(-1);

struct WorkingGeometryBuildResult {
    bool success = false;
    WorkingGeometry working;
    std::vector<AdapterDiagnostic> diagnostics;
};

// General-purpose invocation-local staging builder for Phoenix working meshes.
class WorkingGeometryBuilder {
public:
    explicit WorkingGeometryBuilder(RunElementIdAllocator& ids) noexcept;

    [[nodiscard]] WorkingVertexIndex add_vertex(Point3d point);
    void set_vertex_id(WorkingVertexIndex vertex, VertexId id);
    [[nodiscard]] WorkingFaceIndex begin_facet();
    bool add_vertex_to_facet(WorkingVertexIndex vertex);
    bool end_facet();
    void set_face_label(WorkingFaceIndex face, LabelId label);
    void set_face_tag(WorkingFaceIndex face, std::int32_t tag);
    void set_halfedge_label_by_target(
        WorkingFaceIndex face, WorkingVertexIndex target, LabelId label);
    void set_halfedge_id_by_target(
        WorkingFaceIndex face, WorkingVertexIndex target, HalfedgeId id);
    void set_halfedge_id_by_vertices(
        WorkingVertexIndex source, WorkingVertexIndex target, HalfedgeId id);
    void set_edge_id_by_vertices(
        WorkingVertexIndex source, WorkingVertexIndex target, EdgeId id);
    [[nodiscard]] WorkingGeometryBuildResult build() const;

private:
    struct VertexRecord { Point3d point; VertexId id; };
    struct DirectedMetadata {
        WorkingVertexIndex target;
        LabelId label = unassigned_label_id;
        HalfedgeId id;
        EdgeId edge_id;
    };
    struct FaceRecord {
        std::vector<WorkingVertexIndex> vertices;
        FaceId id;
        LabelId label = unassigned_label_id;
        std::int32_t tag = 0;
        std::vector<DirectedMetadata> directed;
        bool closed = false;
    };

    RunElementIdAllocator* ids_;
    std::vector<VertexRecord> vertices_;
    std::vector<FaceRecord> faces_;
    WorkingFaceIndex open_face_ = invalid_working_face_index;
};

} // namespace phoenix
