#include "phoenix/working_geometry.hpp"

#include <cstdlib>
#include <iostream>

namespace {

phoenix::CanonicalGeometryRef make_two_triangles(double near_offset = 1.0)
{
    std::vector<phoenix::RuntimeVertex> vertices{
        {{0, 0, 0}, phoenix::VertexId{1}, 0},
        {{1, 0, 0}, phoenix::VertexId{2}, 1},
        {{0, 0, 1}, phoenix::VertexId{3}, 2},
        {{near_offset, 0, 0}, phoenix::VertexId{4}, 3},
    };
    std::vector<phoenix::RuntimeHalfedge> halfedges{
        {0, 0, 1, 2, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{5}, phoenix::EdgeId{11}, phoenix::LabelId{101}},
        {1, 0, 2, 0, 3, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{6}, phoenix::EdgeId{12}, phoenix::LabelId{102}},
        {2, 0, 0, 1, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{7}, phoenix::EdgeId{13}, phoenix::LabelId{103}},
        {2, 1, 4, 5, 1, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{8}, phoenix::EdgeId{12}, phoenix::LabelId{202}},
        {1, 1, 5, 3, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{9}, phoenix::EdgeId{14}, phoenix::LabelId{203}},
        {3, 1, 3, 4, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{10}, phoenix::EdgeId{15}, phoenix::LabelId{204}},
    };
    std::vector<phoenix::RuntimeFace> faces{
        {0, phoenix::FaceId{16}, phoenix::LabelId{301}},
        {3, phoenix::FaceId{17}, phoenix::LabelId{302}},
    };
    return phoenix::CanonicalGeometry::create(
        std::move(vertices), std::move(halfedges), std::move(faces));
}

bool has_diagnostic(const std::vector<phoenix::AdapterDiagnostic>& diagnostics,
    phoenix::AdapterDiagnosticCode code)
{
    for (const auto& item : diagnostics) if (item.code == code) return true;
    return false;
}

bool test_labeled_round_trip()
{
    const auto source = make_two_triangles();
    const phoenix::SurfaceMeshAdapter adapter;
    const auto promoted = adapter.promote(*source);
    if (!promoted.success) return false;
    const auto demoted = adapter.demote(promoted.working);
    if (!demoted.success()) return false;
    const phoenix::RuntimeHalfedge* forward = nullptr;
    const phoenix::RuntimeHalfedge* reverse = nullptr;
    for (const auto& halfedge : demoted.geometry->halfedges()) {
        if (halfedge.id == phoenix::HalfedgeId{6}) forward = &halfedge;
        if (halfedge.id == phoenix::HalfedgeId{8}) reverse = &halfedge;
    }
    return demoted.geometry->vertices().size() == source->vertices().size()
        && demoted.geometry->faces().size() == source->faces().size()
        && demoted.geometry->halfedges().size() == source->halfedges().size()
        && forward != nullptr && reverse != nullptr
        && forward->label == phoenix::LabelId{102}
        && reverse->label == phoenix::LabelId{202}
        && forward->opposite != phoenix::invalid_geometry_index
        && demoted.geometry->halfedges()[forward->opposite].id == reverse->id;
}

bool test_non_manifold_is_item_failure()
{
    std::vector<phoenix::RuntimeVertex> vertices{
        {{0, 0, 0}, phoenix::VertexId{1}, 0},
        {{1, 0, 0}, phoenix::VertexId{2}, 1},
        {{0, 1, 0}, phoenix::VertexId{3}, 2},
    };
    std::vector<phoenix::RuntimeHalfedge> halfedges{
        {0, 0, 0, 0, phoenix::invalid_geometry_index, 1, phoenix::HalfedgeId{4}, phoenix::EdgeId{9}, phoenix::LabelId{1}},
        {1, 1, 1, 1, phoenix::invalid_geometry_index, 2, phoenix::HalfedgeId{5}, phoenix::EdgeId{9}, phoenix::LabelId{2}},
        {2, 2, 2, 2, phoenix::invalid_geometry_index, 0, phoenix::HalfedgeId{6}, phoenix::EdgeId{9}, phoenix::LabelId{3}},
    };
    std::vector<phoenix::RuntimeFace> faces{
        {0, phoenix::FaceId{10}, phoenix::LabelId{4}},
        {1, phoenix::FaceId{11}, phoenix::LabelId{5}},
        {2, phoenix::FaceId{12}, phoenix::LabelId{6}},
    };
    const auto source = phoenix::CanonicalGeometry::create(
        std::move(vertices), std::move(halfedges), std::move(faces));
    const auto result = phoenix::SurfaceMeshAdapter{}.promote(*source);
    return !result.success
        && has_diagnostic(result.diagnostics,
            phoenix::AdapterDiagnosticCode::unsupported_non_manifold_topology);
}

bool test_near_vertex_and_degenerate_repair()
{
    const auto source = make_two_triangles(1.0 + 5e-7);
    phoenix::RepairPolicy policy;
    policy.absolute_tolerance = 1e-6;
    const auto repaired = phoenix::GeometryRepairer{policy}.repair(*source);
    return repaired.success()
        && repaired.geometry->vertices().size() == 3
        && repaired.geometry->faces().size() == 1
        && has_diagnostic(repaired.diagnostics,
            phoenix::AdapterDiagnosticCode::merged_near_vertices)
        && has_diagnostic(repaired.diagnostics,
            phoenix::AdapterDiagnosticCode::removed_zero_length_edge)
        && has_diagnostic(repaired.diagnostics,
            phoenix::AdapterDiagnosticCode::removed_zero_area_face)
        && has_diagnostic(repaired.diagnostics,
            phoenix::AdapterDiagnosticCode::removed_labeled_topology);
}

bool test_version_and_invalid_tolerance()
{
    phoenix::RepairPolicy policy;
    policy.absolute_tolerance = -1.0;
    const phoenix::GeometryRepairer repairer{policy};
    const phoenix::SurfaceMeshAdapter adapter;
    return repairer.policy().absolute_tolerance == 1e-6
        && adapter.version().adapter == "surface-mesh-v1"
        && repairer.policy().version.repair == "absolute-repair-v1";
}

bool test_direct_extrusion_face_preparation()
{
    const auto source = make_two_triangles();
    const auto prepared = phoenix::ExtrusionInputAdapter{}.prepare_face(*source, 0);
    return prepared.success && prepared.face.boundary.size() == 3
        && prepared.face.source_face_id == phoenix::FaceId{16}
        && prepared.face.face_label == phoenix::LabelId{301}
        && prepared.face.boundary[0].source_vertex_id == phoenix::VertexId{1}
        && prepared.face.boundary[0].source_halfedge_id == phoenix::HalfedgeId{5}
        && prepared.face.boundary[0].directed_edge_label == phoenix::LabelId{101};
}

} // namespace

int main()
{
    const bool round_trip = test_labeled_round_trip();
    const bool non_manifold = test_non_manifold_is_item_failure();
    const bool repair = test_near_vertex_and_degenerate_repair();
    const bool version = test_version_and_invalid_tolerance();
    const bool extrusion = test_direct_extrusion_face_preparation();
    std::cout << "round trip: " << round_trip << '\n'
              << "non-manifold rejection: " << non_manifold << '\n'
              << "repair: " << repair << '\n'
              << "version: " << version << '\n'
              << "extrusion preparation: " << extrusion << '\n';
    const bool ok = round_trip && non_manifold && repair && version && extrusion;
    if (!ok) {
        std::cerr << "working geometry tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "working geometry tests passed\n";
    return EXIT_SUCCESS;
}
