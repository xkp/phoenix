#pragma once

#include "phoenix/geometry.hpp"

#include <CGAL/Simple_cartesian.h>
#include <CGAL/Surface_mesh.h>

#include <cstdint>
#include <string>
#include <vector>

namespace phoenix {

enum class WorkingTopology {
    oriented_manifold_surface,
};

struct GeometryPolicyVersion {
    std::string adapter = "surface-mesh-v1";
    std::string repair = "absolute-repair-v1";
};

struct RepairPolicy {
    // Production's smallest extrusion coordinates are serialized to about
    // 1e-5. Start one order below that observed precision.
    double absolute_tolerance = 1e-6;
    GeometryPolicyVersion version;
};

enum class AdapterDiagnosticCode {
    invalid_runtime_geometry,
    unsupported_non_manifold_topology,
    unsupported_hole,
    orientation_conflict,
    cgal_face_insertion_failed,
    malformed_working_geometry,
    removed_zero_length_edge,
    removed_zero_area_face,
    merged_near_vertices,
    removed_labeled_topology,
};

struct AdapterDiagnostic {
    AdapterDiagnosticCode code;
    std::string message;
    GeometryIndex source_element = invalid_geometry_index;
};

using WorkingKernel = CGAL::Simple_cartesian<double>;
using WorkingPoint = WorkingKernel::Point_3;
using WorkingSurfaceMesh = CGAL::Surface_mesh<WorkingPoint>;

struct WorkingGeometry {
    WorkingSurfaceMesh mesh;
    WorkingSurfaceMesh::Property_map<WorkingSurfaceMesh::Vertex_index, std::uint64_t> vertex_ids;
    WorkingSurfaceMesh::Property_map<WorkingSurfaceMesh::Vertex_index, GeometryIndex> source_vertices;
    WorkingSurfaceMesh::Property_map<WorkingSurfaceMesh::Halfedge_index, std::uint64_t> halfedge_ids;
    WorkingSurfaceMesh::Property_map<WorkingSurfaceMesh::Halfedge_index, std::uint64_t> edge_ids;
    WorkingSurfaceMesh::Property_map<WorkingSurfaceMesh::Halfedge_index, std::int32_t> halfedge_labels;
    WorkingSurfaceMesh::Property_map<WorkingSurfaceMesh::Halfedge_index, GeometryIndex> source_halfedges;
    WorkingSurfaceMesh::Property_map<WorkingSurfaceMesh::Face_index, std::uint64_t> face_ids;
    WorkingSurfaceMesh::Property_map<WorkingSurfaceMesh::Face_index, std::int32_t> face_labels;
    WorkingSurfaceMesh::Property_map<WorkingSurfaceMesh::Face_index, GeometryIndex> source_faces;
};

struct PromotionResult {
    bool success = false;
    WorkingGeometry working;
    std::vector<AdapterDiagnostic> diagnostics;
};

struct DemotionResult {
    CanonicalGeometryRef geometry;
    std::vector<AdapterDiagnostic> diagnostics;
    [[nodiscard]] bool success() const noexcept { return geometry != nullptr; }
};

struct ExtrusionWorkingPoint {
    Point3d point;
    VertexId source_vertex_id;
    HalfedgeId source_halfedge_id;
    EdgeId source_edge_id;
    LabelId directed_edge_label = unassigned_label_id;
};

struct ExtrusionWorkingFace {
    FaceId source_face_id;
    LabelId face_label = unassigned_label_id;
    std::vector<ExtrusionWorkingPoint> boundary;
};

struct ExtrusionPreparationResult {
    bool success = false;
    ExtrusionWorkingFace face;
    std::vector<AdapterDiagnostic> diagnostics;
};

class ExtrusionInputAdapter {
public:
    [[nodiscard]] ExtrusionPreparationResult prepare_face(
        const CanonicalGeometry& source,
        GeometryIndex face_index) const;
};

class SurfaceMeshAdapter {
public:
    [[nodiscard]] WorkingTopology accepted_topology() const noexcept
    {
        return WorkingTopology::oriented_manifold_surface;
    }
    [[nodiscard]] const GeometryPolicyVersion& version() const noexcept { return version_; }
    [[nodiscard]] PromotionResult promote(const CanonicalGeometry& source) const;
    [[nodiscard]] DemotionResult demote(const WorkingGeometry& source) const;

private:
    GeometryPolicyVersion version_;
};

class GeometryRepairer {
public:
    explicit GeometryRepairer(RepairPolicy policy = {});
    [[nodiscard]] const RepairPolicy& policy() const noexcept { return policy_; }
    [[nodiscard]] DemotionResult repair(const CanonicalGeometry& source) const;

private:
    RepairPolicy policy_;
};

[[nodiscard]] std::string to_string(AdapterDiagnosticCode code);

} // namespace phoenix
