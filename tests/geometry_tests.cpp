#include "phoenix/geometry.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

phoenix::CanonicalGeometryRef make_triangle(
    phoenix::LabelId face_label = phoenix::LabelId{10},
    phoenix::LabelId first_edge_label = phoenix::LabelId{20})
{
    std::vector<phoenix::RuntimeVertex> vertices{
        {{0, 0, 0}, phoenix::VertexId{1}, 0},
        {{1, 0, 0}, phoenix::VertexId{2}, 1},
        {{0, 0, 1}, phoenix::VertexId{3}, 2},
    };
    std::vector<phoenix::RuntimeHalfedge> halfedges{
        {0, 0, 1, 2, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{4}, phoenix::EdgeId{7}, first_edge_label},
        {1, 0, 2, 0, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{5}, phoenix::EdgeId{8}, phoenix::LabelId{21}},
        {2, 0, 0, 1, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{6}, phoenix::EdgeId{9}, phoenix::LabelId{22}},
    };
    std::vector<phoenix::RuntimeFace> faces{{0, phoenix::FaceId{10}, face_label}};
    return phoenix::CanonicalGeometry::create(
        std::move(vertices), std::move(halfedges), std::move(faces));
}

bool test_labeled_polygon_and_deterministic_serialization()
{
    const auto first = make_triangle();
    const auto second = make_triangle();
    const auto round_trip = phoenix::CanonicalGeometry::deserialize_canonical(
        first->serialize_canonical());
    return first != nullptr && second != nullptr
        && round_trip != nullptr
        && first->serialize_canonical() == second->serialize_canonical()
        && first->serialize_canonical() == round_trip->serialize_canonical()
        && first->fingerprint() == second->fingerprint()
        && first->fingerprint() == round_trip->fingerprint()
        && first->faces()[0].label == phoenix::LabelId{10}
        && first->halfedges()[0].label == phoenix::LabelId{20};
}

bool test_opposite_halfedges_keep_distinct_labels()
{
    std::vector<phoenix::RuntimeVertex> vertices{
        {{0, 0, 0}, phoenix::VertexId{1}, 0},
        {{1, 0, 0}, phoenix::VertexId{2}, 1},
    };
    std::vector<phoenix::RuntimeHalfedge> halfedges{
        {0, 0, 0, 0, 1, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{3}, phoenix::EdgeId{5}, phoenix::LabelId{30}},
        {1, 1, 1, 1, 0, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{4}, phoenix::EdgeId{5}, phoenix::LabelId{31}},
    };
    std::vector<phoenix::RuntimeFace> faces{
        {0, phoenix::FaceId{6}, phoenix::LabelId{40}},
        {1, phoenix::FaceId{7}, phoenix::LabelId{41}},
    };
    const auto geometry = phoenix::CanonicalGeometry::create(
        std::move(vertices), std::move(halfedges), std::move(faces));
    return geometry != nullptr
        && geometry->halfedges()[0].label != geometry->halfedges()[1].label;
}

bool test_non_manifold_radial_edge_is_representable()
{
    std::vector<phoenix::RuntimeVertex> vertices{
        {{0, 0, 0}, phoenix::VertexId{1}, 0},
        {{1, 0, 0}, phoenix::VertexId{2}, 1},
        {{0, 1, 0}, phoenix::VertexId{3}, 2},
    };
    std::vector<phoenix::RuntimeHalfedge> halfedges{
        {0, 0, 0, 0, phoenix::invalid_geometry_index, 1,
            phoenix::HalfedgeId{4}, phoenix::EdgeId{9}, phoenix::LabelId{1}},
        {1, 1, 1, 1, phoenix::invalid_geometry_index, 2,
            phoenix::HalfedgeId{5}, phoenix::EdgeId{9}, phoenix::LabelId{2}},
        {2, 2, 2, 2, phoenix::invalid_geometry_index, 0,
            phoenix::HalfedgeId{6}, phoenix::EdgeId{9}, phoenix::LabelId{3}},
    };
    std::vector<phoenix::RuntimeFace> faces{
        {0, phoenix::FaceId{10}, phoenix::LabelId{4}},
        {1, phoenix::FaceId{11}, phoenix::LabelId{5}},
        {2, phoenix::FaceId{12}, phoenix::LabelId{6}},
    };
    return phoenix::CanonicalGeometry::create(
        std::move(vertices), std::move(halfedges), std::move(faces)) != nullptr;
}

bool test_hole_is_rejected()
{
    auto triangle = make_triangle();
    std::vector<phoenix::RuntimeVertex> vertices = triangle->vertices();
    std::vector<phoenix::RuntimeHalfedge> halfedges = triangle->halfedges();
    auto extra = halfedges[0];
    extra.next = 3;
    extra.previous = 3;
    extra.id = phoenix::HalfedgeId{100};
    halfedges.push_back(extra);
    std::vector<phoenix::RuntimeFace> faces = triangle->faces();
    phoenix::GeometryValidationResult validation;
    const auto invalid = phoenix::CanonicalGeometry::create(
        std::move(vertices), std::move(halfedges), std::move(faces), &validation);
    if (invalid != nullptr) return false;
    for (const auto& issue : validation.issues) {
        if (issue.code == phoenix::GeometryValidationCode::hole_not_supported) return true;
    }
    return false;
}

bool test_non_finite_is_rejected()
{
    std::vector<phoenix::RuntimeVertex> vertices{
        {{std::numeric_limits<double>::infinity(), 0, 0}, phoenix::VertexId{1}, 0}};
    std::vector<phoenix::RuntimeHalfedge> halfedges{
        {0, 0, 0, 0, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{2}, phoenix::EdgeId{3}, phoenix::LabelId{1}}};
    std::vector<phoenix::RuntimeFace> faces{{0, phoenix::FaceId{4}, phoenix::LabelId{2}}};
    phoenix::GeometryValidationResult validation;
    return phoenix::CanonicalGeometry::create(
        std::move(vertices), std::move(halfedges), std::move(faces), &validation) == nullptr
        && !validation.ok();
}

bool test_fingerprint_covers_coordinates_orientation_and_labels()
{
    const auto base = make_triangle();
    const auto changed_face = make_triangle(phoenix::LabelId{11});
    const auto changed_edge = make_triangle(phoenix::LabelId{10}, phoenix::LabelId{23});
    std::vector<phoenix::RuntimeVertex> vertices = base->vertices();
    vertices[0].point.x = 0.25;
    const auto changed_position = phoenix::CanonicalGeometry::create(
        std::move(vertices), base->halfedges(), base->faces());
    return base->fingerprint() != changed_face->fingerprint()
        && base->fingerprint() != changed_edge->fingerprint()
        && base->fingerprint() != changed_position->fingerprint();
}

bool test_immutable_sharing_selection_and_run_ids()
{
    const auto geometry = make_triangle();
    phoenix::FaceReference reference{geometry, 0, geometry->faces()[0].id};
    phoenix::RunElementIdAllocator ids;
    const auto vertex = ids.next_vertex();
    const auto halfedge = ids.next_halfedge();
    const auto edge = ids.next_edge();
    const auto face = ids.next_face();
    const auto mapped = phoenix::point_from_legacy_2d(2.0, 3.0);
    return reference.valid() && geometry.use_count() >= 2
        && vertex.value() != halfedge.value() && halfedge.value() != edge.value()
        && edge.value() != face.value() && mapped.x == 2.0 && mapped.y == 0.0 && mapped.z == 3.0;
}

bool test_runtime_storage_estimate_covers_payload()
{
    const auto geometry = make_triangle();
    const auto minimum = sizeof(phoenix::CanonicalGeometry)
        + geometry->vertices().size() * sizeof(phoenix::RuntimeVertex)
        + geometry->halfedges().size() * sizeof(phoenix::RuntimeHalfedge)
        + geometry->faces().size() * sizeof(phoenix::RuntimeFace);
    return geometry->storage_bytes() >= minimum;
}

} // namespace

int main()
{
    const bool ok = test_labeled_polygon_and_deterministic_serialization()
        && test_opposite_halfedges_keep_distinct_labels()
        && test_non_manifold_radial_edge_is_representable()
        && test_hole_is_rejected()
        && test_non_finite_is_rejected()
        && test_fingerprint_covers_coordinates_orientation_and_labels()
        && test_immutable_sharing_selection_and_run_ids()
        && test_runtime_storage_estimate_covers_payload();
    if (!ok) {
        std::cerr << "geometry tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "geometry tests passed\n";
    return EXIT_SUCCESS;
}
