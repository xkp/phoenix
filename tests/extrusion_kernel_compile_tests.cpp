#include "phoenix/extrusion/kernel.hpp"
#include "phoenix/extrusion/output_builder.hpp"

#include <cstdlib>
#include <iostream>

int main()
{
    phoenix::RunElementIdAllocator ids{1000};
    const auto invalid = phoenix::extrusion::run_kernel({}, ids);
    const bool invalid_ok = !invalid.success && invalid.diagnostics.size() == 1
        && invalid.diagnostics[0].code == phoenix::extrusion::KernelErrorCode::invalid_input
        && phoenix::extrusion::to_string(invalid.diagnostics[0].code) == "invalid_input";

    phoenix::extrusion::ProfileSegment segment;
    segment.delta_y = 1.0;
    segment.face_label = phoenix::LabelId{20};
    segment.left_label = phoenix::LabelId{21};
    segment.bottom_label = phoenix::LabelId{22};
    segment.right_label = phoenix::LabelId{23};
    segment.top_label = phoenix::LabelId{24};
    segment.skirt_label = phoenix::LabelId{25};
    const auto profile = phoenix::extrusion::Profile::create({segment});

    auto opposite_segment = segment;
    opposite_segment.delta_y = -1.0;
    const auto opposite_profile = phoenix::extrusion::Profile::create({opposite_segment});

    phoenix::extrusion::KernelExtrusionInput input;
    input.sign = profile->sign();
    input.bottom_label = phoenix::LabelId{30};
    input.right_label = phoenix::LabelId{31};
    input.top_label = phoenix::LabelId{32};
    input.left_label = phoenix::LabelId{33};
    input.skirt_label = phoenix::LabelId{34};
    input.cap_label = phoenix::LabelId{35};
    input.boundary = {
        {{0, 0, 0}, profile, phoenix::LabelId{40}, phoenix::VertexId{1}, phoenix::HalfedgeId{11}, phoenix::EdgeId{101}},
        {{1, 0, 0}, profile, phoenix::LabelId{41}, phoenix::VertexId{2}, phoenix::HalfedgeId{12}, phoenix::EdgeId{102}},
        {{0, 1, 0}, profile, phoenix::LabelId{42}, phoenix::VertexId{3}, phoenix::HalfedgeId{13}, phoenix::EdgeId{103}},
    };
    auto inconsistent_input = input;
    inconsistent_input.boundary[1].profile = opposite_profile;
    const auto inconsistent = phoenix::extrusion::run_kernel(inconsistent_input, ids);
    const bool sign_ok = !inconsistent.success && inconsistent.diagnostics.size() == 1
        && inconsistent.diagnostics[0].code
            == phoenix::extrusion::KernelErrorCode::inconsistent_profile_sign;

    const auto extruded = phoenix::extrusion::run_kernel(input, ids);
    std::size_t side_faces = 0;
    std::size_t cap_faces = 0;
    for (const auto face : extruded.working.mesh.faces()) {
        if (extruded.working.face_tags[face] == phoenix::extrusion::side_face_tag) ++side_faces;
        if (extruded.working.face_tags[face] == phoenix::extrusion::cap_face_tag) ++cap_faces;
    }
    std::size_t source_vertices = 0;
    for (const auto vertex : extruded.working.mesh.vertices())
        if (extruded.working.vertex_ids[vertex] >= 1
            && extruded.working.vertex_ids[vertex] <= 3) ++source_vertices;
    std::size_t source_halfedges = 0;
    std::size_t source_edges = 0;
    std::size_t bottom_labels = 0;
    std::size_t right_labels = 0;
    std::size_t top_labels = 0;
    std::size_t left_labels = 0;
    std::size_t cap_vertex_labels = 0;
    for (const auto halfedge : extruded.working.mesh.halfedges()) {
        if (extruded.working.halfedge_ids[halfedge] >= 11
            && extruded.working.halfedge_ids[halfedge] <= 13) ++source_halfedges;
        if (extruded.working.edge_ids[halfedge] >= 101
            && extruded.working.edge_ids[halfedge] <= 103) ++source_edges;
        if (extruded.working.mesh.face(halfedge) == phoenix::WorkingSurfaceMesh::null_face())
            continue;
        const auto label = extruded.working.halfedge_labels[halfedge];
        if (label == 22) ++bottom_labels;
        if (label == 23) ++right_labels;
        if (label == 24) ++top_labels;
        if (label == 21) ++left_labels;
        if (label >= 40 && label <= 42) ++cap_vertex_labels;
    }
    const auto demoted = phoenix::SurfaceMeshAdapter{}.demote(extruded.working);
    const bool direct_ok = extruded.success
        && extruded.working.mesh.number_of_vertices() == 6
        && extruded.working.mesh.number_of_faces() == 4
        && side_faces == 3 && cap_faces == 1
        && source_vertices == 3 && source_halfedges == 3 && source_edges == 6
        && bottom_labels == 3 && right_labels == 3
        && top_labels == 3 && left_labels == 3 && cap_vertex_labels == 3
        && demoted.success();

    auto collision_segment = segment;
    collision_segment.delta_x = -1.0;
    collision_segment.delta_y = 1.0;
    collision_segment.face_label = phoenix::LabelId{90};
    const auto collision_profile = phoenix::extrusion::Profile::create({collision_segment});
    phoenix::extrusion::KernelExtrusionInput collision_input;
    collision_input.sign = collision_profile->sign();
    collision_input.cap_label = phoenix::LabelId{95};
    collision_input.boundary = {
        {{0, 0, 0}, collision_profile, phoenix::LabelId{96}, phoenix::VertexId{301}, phoenix::HalfedgeId{311}, phoenix::EdgeId{321}},
        {{1, 0, 0}, collision_profile, phoenix::LabelId{96}, phoenix::VertexId{302}, phoenix::HalfedgeId{312}, phoenix::EdgeId{322}},
        {{1, 1, 0}, collision_profile, phoenix::LabelId{96}, phoenix::VertexId{303}, phoenix::HalfedgeId{313}, phoenix::EdgeId{323}},
        {{0, 1, 0}, collision_profile, phoenix::LabelId{96}, phoenix::VertexId{304}, phoenix::HalfedgeId{314}, phoenix::EdgeId{324}},
    };
    phoenix::RunElementIdAllocator collision_ids_a{1000};
    phoenix::RunElementIdAllocator collision_ids_b{1000};
    const auto collided = phoenix::extrusion::run_kernel(collision_input, collision_ids_a);
    const auto collided_repeat = phoenix::extrusion::run_kernel(
        collision_input, collision_ids_b);
    const auto collided_demoted = collided.success
        ? phoenix::SurfaceMeshAdapter{}.demote(collided.working)
        : phoenix::DemotionResult{};
    const auto collided_repeat_demoted = collided_repeat.success
        ? phoenix::SurfaceMeshAdapter{}.demote(collided_repeat.working)
        : phoenix::DemotionResult{};
    const bool collision_ok = collided.success && collided_demoted.success()
        && collided.working.mesh.number_of_vertices() == 5
        && collided.working.mesh.number_of_faces() == 4
        && collided_repeat_demoted.success()
        && collided_demoted.geometry->fingerprint()
            == collided_repeat_demoted.geometry->fingerprint();

    auto horizontal_segment = segment;
    horizontal_segment.delta_x = -0.25;
    horizontal_segment.delta_y = 0.0;
    horizontal_segment.face_label = phoenix::LabelId{100};
    horizontal_segment.skirt_label = phoenix::LabelId{105};
    auto vertical_segment = segment;
    vertical_segment.delta_x = 0.0;
    vertical_segment.delta_y = 1.0;
    vertical_segment.face_label = phoenix::LabelId{101};
    vertical_segment.skirt_label = phoenix::LabelId{106};
    const auto skirt_profile = phoenix::extrusion::Profile::create(
        {horizontal_segment, vertical_segment});
    auto skirt_input = collision_input;
    skirt_input.sign = skirt_profile->sign();
    for (auto& corner : skirt_input.boundary) corner.profile = skirt_profile;
    phoenix::RunElementIdAllocator skirt_ids{2000};
    const auto skirted = phoenix::extrusion::run_kernel(skirt_input, skirt_ids);
    const auto skirted_demoted = skirted.success
        ? phoenix::SurfaceMeshAdapter{}.demote(skirted.working)
        : phoenix::DemotionResult{};
    std::size_t horizontal_faces = 0;
    std::size_t vertical_faces = 0;
    std::size_t skirt_labels = 0;
    for (const auto face : skirted.working.mesh.faces()) {
        if (skirted.working.face_labels[face] == 100) ++horizontal_faces;
        if (skirted.working.face_labels[face] == 101) ++vertical_faces;
        auto halfedge = skirted.working.mesh.halfedge(face);
        do {
            const auto label = skirted.working.halfedge_labels[halfedge];
            if (label == 105 || label == 106) ++skirt_labels;
            halfedge = skirted.working.mesh.next(halfedge);
        } while (halfedge != skirted.working.mesh.halfedge(face));
    }
    const bool skirt_ok = skirted.success && skirted_demoted.success()
        && skirted.working.mesh.number_of_vertices() == 12
        && skirted.working.mesh.number_of_faces() == 9
        && horizontal_faces == 4 && vertical_faces == 4;

    auto skirt_a_segment = segment;
    skirt_a_segment.delta_x = 0.0;
    skirt_a_segment.delta_y = 1.0;
    skirt_a_segment.skirt_label = phoenix::LabelId{115};
    auto skirt_b_segment = skirt_a_segment;
    skirt_b_segment.delta_x = -0.25;
    skirt_b_segment.skirt_label = phoenix::LabelId{116};
    const auto skirt_a = phoenix::extrusion::Profile::create({skirt_a_segment});
    const auto skirt_b = phoenix::extrusion::Profile::create({skirt_b_segment});
    phoenix::extrusion::KernelExtrusionInput collinear_input;
    collinear_input.sign = CGAL::POSITIVE;
    collinear_input.skirt_label = phoenix::LabelId{117};
    collinear_input.cap_label = phoenix::LabelId{118};
    collinear_input.boundary = {
        {{0, 0, 0}, skirt_a, phoenix::LabelId{119}, phoenix::VertexId{401}, phoenix::HalfedgeId{411}, phoenix::EdgeId{421}},
        {{0.5, 0, 0}, skirt_a, phoenix::LabelId{119}, phoenix::VertexId{402}, phoenix::HalfedgeId{412}, phoenix::EdgeId{422}},
        {{1, 0, 0}, skirt_b, phoenix::LabelId{119}, phoenix::VertexId{403}, phoenix::HalfedgeId{413}, phoenix::EdgeId{423}},
        {{1, 1, 0}, skirt_a, phoenix::LabelId{119}, phoenix::VertexId{404}, phoenix::HalfedgeId{414}, phoenix::EdgeId{424}},
        {{0, 1, 0}, skirt_a, phoenix::LabelId{119}, phoenix::VertexId{405}, phoenix::HalfedgeId{415}, phoenix::EdgeId{425}},
    };
    phoenix::RunElementIdAllocator collinear_ids{3000};
    phoenix::RunElementIdAllocator collinear_repeat_ids{3000};
    const auto collinear = phoenix::extrusion::run_kernel(collinear_input, collinear_ids);
    const auto collinear_repeat = phoenix::extrusion::run_kernel(
        collinear_input, collinear_repeat_ids);
    std::size_t explicit_skirt_labels = 0;
    if (collinear.success) {
        for (const auto halfedge : collinear.working.mesh.halfedges()) {
            const auto label = collinear.working.halfedge_labels[halfedge];
            if (label == 115 || label == 116 || label == 117) ++explicit_skirt_labels;
        }
    }
    const auto collinear_demoted = collinear.success
        ? phoenix::SurfaceMeshAdapter{}.demote(collinear.working)
        : phoenix::DemotionResult{};
    const auto collinear_repeat_demoted = collinear_repeat.success
        ? phoenix::SurfaceMeshAdapter{}.demote(collinear_repeat.working)
        : phoenix::DemotionResult{};
    const bool explicit_skirt_ok = collinear.success
        && collinear.working.mesh.number_of_vertices() == 16
        && collinear.working.mesh.number_of_faces() == 13
        && explicit_skirt_labels == 1
        && collinear_demoted.success() && collinear_repeat_demoted.success()
        && collinear_demoted.geometry->fingerprint()
            == collinear_repeat_demoted.geometry->fingerprint();

    std::vector<phoenix::extrusion::ProfileSegment> oracle_segments;
    oracle_segments.push_back({0.0, 6.681003584229391});
    oracle_segments.push_back({4.745519713261649, 3.211469534050179});
    oracle_segments.push_back({0.0, 8.258064516129034});
    auto oracle_horizontal = phoenix::extrusion::ProfileSegment{
        -11.670250896057348, 0.0};
    oracle_horizontal.face_label = phoenix::LabelId{500};
    oracle_segments.push_back(oracle_horizontal);
    const auto oracle_profile = phoenix::extrusion::Profile::create(
        std::move(oracle_segments));
    phoenix::extrusion::KernelExtrusionInput oracle_input;
    oracle_input.sign = oracle_profile->sign();
    oracle_input.cap_label = phoenix::LabelId{501};
    const std::vector<phoenix::Point3d> oracle_points{
        {10.953405017921147, 18.06451612903226, 0},
        {10.953405017921147, 13.792114695340501, 0},
        {20.64516129032258, 7.311827956989247, 0},
        {28.27240143369176, 13.620071684587813, 0},
        {28.27240143369176, 17.892473118279568, 0},
        {13.460166468489893, 16.646848989298455, 0},
    };
    for (std::size_t i = 0; i < oracle_points.size(); ++i) {
        oracle_input.boundary.push_back({oracle_points[i], oracle_profile,
            phoenix::LabelId{501}, phoenix::VertexId{600 + i},
            phoenix::HalfedgeId{610 + i}, phoenix::EdgeId{620 + i}});
    }
    phoenix::RunElementIdAllocator oracle_ids{4000};
    const auto oracle = phoenix::extrusion::run_kernel(oracle_input, oracle_ids);
    const auto oracle_demoted = oracle.success
        ? phoenix::SurfaceMeshAdapter{}.demote(oracle.working)
        : phoenix::DemotionResult{};
    std::size_t oracle_triangles = 0;
    std::size_t oracle_sides = 0;
    std::size_t oracle_caps = 0;
    if (oracle.success) {
        for (const auto face : oracle.working.mesh.faces()) {
            std::size_t degree = 0;
            auto halfedge = oracle.working.mesh.halfedge(face);
            do {
                ++degree;
                halfedge = oracle.working.mesh.next(halfedge);
            } while (halfedge != oracle.working.mesh.halfedge(face));
            oracle_triangles += degree - 2;
            if (oracle.working.face_tags[face] == phoenix::extrusion::side_face_tag)
                ++oracle_sides;
            if (oracle.working.face_tags[face] == phoenix::extrusion::cap_face_tag)
                ++oracle_caps;
        }
    }
    const bool oracle_ok = oracle.success && oracle_demoted.success()
        && oracle.working.mesh.number_of_vertices() == 24
        && oracle.working.mesh.number_of_faces() == 19
        && oracle_triangles == 40 && oracle_sides == 18 && oracle_caps == 1;

    std::cout << "invalid boundary: " << invalid_ok << '\n'
              << "profile sign validation: " << sign_ok << '\n'
              << "direct triangle extrusion: " << direct_ok
              << " (vertices=" << extruded.working.mesh.number_of_vertices()
              << ", faces=" << extruded.working.mesh.number_of_faces()
              << ", sides=" << side_faces << ", caps=" << cap_faces
              << ", source vertices=" << source_vertices
              << ", source halfedges=" << source_halfedges
              << ", source edge incidences=" << source_edges
              << ", demoted=" << demoted.success() << ")\n"
              << "shrinking-square collision: " << collision_ok
              << " (vertices=" << collided.working.mesh.number_of_vertices()
              << ", faces=" << collided.working.mesh.number_of_faces()
              << ", fingerprint=" << (collided_demoted.success()
                    ? collided_demoted.geometry->fingerprint() : 0) << ")\n"
              << "horizontal profile transition: " << skirt_ok
              << " (vertices=" << skirted.working.mesh.number_of_vertices()
              << ", faces=" << skirted.working.mesh.number_of_faces()
              << ", horizontal faces=" << horizontal_faces
              << ", vertical faces=" << vertical_faces
              << ", skirt labels=" << skirt_labels
              << ", fingerprint=" << (skirted_demoted.success()
                    ? skirted_demoted.geometry->fingerprint() : 0) << ")\n"
              << "collinear skirt insertion: " << explicit_skirt_ok
              << " (vertices=" << collinear.working.mesh.number_of_vertices()
              << ", faces=" << collinear.working.mesh.number_of_faces()
              << ", skirt labels=" << explicit_skirt_labels << ")\n"
              << "captured production oracle topology: " << oracle_ok
              << " (vertices=" << oracle.working.mesh.number_of_vertices()
              << ", faces=" << oracle.working.mesh.number_of_faces()
              << ", export triangles=" << oracle_triangles << ")\n";
    return invalid_ok && sign_ok && direct_ok && collision_ok && skirt_ok
        && explicit_skirt_ok && oracle_ok
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
