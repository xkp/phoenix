#include "phoenix/partition/geometry.h"
#include "phoenix/merge/ported/production/merge_borders3.h"
#include "phoenix/merge/ported/production/merge_faces.h"
#include "phoenix/inset/ported/cleanup_face.h"
#include "phoenix/merge/production_pipeline.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <tuple>
#include <vector>

namespace {

class MeshBuilder : public CGAL::Modifier_base<geometry::polyhedron3::HalfedgeDS> {
public:
    MeshBuilder(std::vector<geometry::point3> points,
        std::vector<std::vector<std::size_t>> faces)
        : points_(std::move(points)), faces_(std::move(faces)) {}

    void operator()(geometry::polyhedron3::HalfedgeDS& hds) override
    {
        CGAL::Polyhedron_incremental_builder_3<geometry::polyhedron3::HalfedgeDS>
            builder(hds, true);
        builder.begin_surface(points_.size(), faces_.size());
        for (const auto& point : points_) builder.add_vertex(point);
        for (const auto& face : faces_) {
            builder.begin_facet();
            for (const auto index : face) builder.add_vertex_to_facet(index);
            builder.end_facet();
        }
        builder.end_surface();
    }

private:
    std::vector<geometry::point3> points_;
    std::vector<std::vector<std::size_t>> faces_;
};

geometry::polyhedron3 mesh(std::vector<geometry::point3> points,
    std::vector<std::vector<std::size_t>> faces)
{
    geometry::polyhedron3 result;
    MeshBuilder builder(std::move(points), std::move(faces));
    result.delegate(builder);
    int vertex_id = 1;
    for (auto vertex = result.vertices_begin(); vertex != result.vertices_end(); ++vertex)
        vertex->data.id = vertex_id++;
    int face_id = 10;
    int face_label = 20;
    for (auto face = result.facets_begin(); face != result.facets_end(); ++face) {
        face->data.id = face_id++;
        face->data.label = face_label++;
    }
    int edge_id = 30;
    int edge_label = 40;
    for (auto edge = result.halfedges_begin(); edge != result.halfedges_end(); ++edge) {
        edge->data.id = edge_id++;
        edge->data.label = edge_label++;
    }
    return result;
}

bool run_oracle(geometry::face3_list input, geometry::polyhedron3& output,
    bool join_vertices = false)
{
    using Oracle = merge_borders3<geometry::Kernel, geometry::arrangement2,
        geometry::polyhedron3>;
    int vertex_id = 100;
    int edge_id = 200;
    vertex_id_generator vertices = [&vertex_id] { return vertex_id++; };
    edge_id_generator edges = [&edge_id] { return edge_id++; };
    bool failed = false;
    error_function errors = [&failed](solver_error&) { failed = true; };
    Oracle::run(output, input, join_vertices, errors, vertices, edges);
    return !failed;
}

bool run_oracle(geometry::polyhedron3& source, geometry::polyhedron3& output,
    bool join_vertices = false)
{
    geometry::face3_list input;
    for (auto face = source.facets_begin(); face != source.facets_end(); ++face)
        input.push_back(face);
    return run_oracle(std::move(input), output, join_vertices);
}

using DirectedPointPair = std::tuple<double, double, double, double, double, double>;

std::map<DirectedPointPair, int> directed_labels(geometry::polyhedron3& value)
{
    std::map<DirectedPointPair, int> result;
    for (auto edge = value.halfedges_begin(); edge != value.halfedges_end(); ++edge) {
        const auto& source = edge->opposite()->vertex()->point();
        const auto& target = edge->vertex()->point();
        result[{CGAL::to_double(source.x()), CGAL::to_double(source.y()),
            CGAL::to_double(source.z()), CGAL::to_double(target.x()),
            CGAL::to_double(target.y()), CGAL::to_double(target.z())}]
            = edge->data.label;
    }
    return result;
}

void set_boundary_labels(geometry::polyhedron3& value, int current, int opposite)
{
    for (auto face = value.facets_begin(); face != value.facets_end(); ++face) {
        auto edge = face->facet_begin();
        const auto end = edge;
        do {
            edge->data.label = current;
            edge->opposite()->data.label = opposite;
            ++edge;
        } while (edge != end);
    }
}

bool boundary_labels_are(geometry::polyhedron3& value, int current, int opposite)
{
    for (auto face = value.facets_begin(); face != value.facets_end(); ++face) {
        auto edge = face->facet_begin();
        const auto end = edge;
        do {
            if (edge->data.label != current
                || edge->opposite()->data.label != opposite) return false;
            ++edge;
        } while (edge != end);
    }
    return true;
}

using CleanupOracle = cleanup_face3<geometry::Kernel, geometry::polyhedron3>;

CleanupOracle::request cleanup_request()
{
    CleanupOracle::request request;
    request.merge_predicate = [](geometry::edge3 first, geometry::edge3 second) {
        return first->data.label == second->data.label
            && first->opposite()->data.label == second->opposite()->data.label;
    };
    return request;
}

} // namespace

int main()
{
    using Kernel = geometry::Kernel;
    using Oracle = merge_borders3<Kernel, geometry::arrangement2,
        geometry::polyhedron3>;

    geometry::polyhedron3 output;
    geometry::face3_list input;
    int vertex_id = 100;
    int edge_id = 200;
    vertex_id_generator vertices = [&vertex_id] { return vertex_id++; };
    edge_id_generator edges = [&edge_id] { return edge_id++; };
    error_function errors = [](solver_error&) {};
    Oracle::run(output, input, false, errors, vertices, edges);

    const bool empty_is_noop = output.empty();

    auto disconnected_source = mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0},
        {3, 0, 0}, {4, 0, 0}, {3, 1, 0}}, {{0, 1, 2}, {3, 4, 5}});
    geometry::polyhedron3 disconnected_output;
    const auto disconnected_labels = directed_labels(disconnected_source);
    const bool disconnected = run_oracle(disconnected_source, disconnected_output)
        && disconnected_output.size_of_facets() == 2
        && disconnected_output.size_of_vertices() == 6;
    const bool labels = disconnected
        && directed_labels(disconnected_output) == disconnected_labels;

    auto shared_source = mesh({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
        {{0, 1, 2}, {0, 2, 3}});
    geometry::polyhedron3 shared_output;
    const bool shared = run_oracle(shared_source, shared_output)
        && shared_output.size_of_facets() == 2
        && shared_output.size_of_vertices() == 4
        && shared_output.is_valid(false, 0);

    auto duplicate_source = mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}},
        {{0, 1, 2}});
    geometry::face3_list duplicate_input{
        duplicate_source.facets_begin(), duplicate_source.facets_begin()};
    geometry::polyhedron3 duplicate_output;
    const bool duplicate_ran = run_oracle(duplicate_input, duplicate_output);
    const auto duplicate_faces = duplicate_output.size_of_facets();
    const bool duplicate_references_are_repeated = duplicate_ran
        && duplicate_faces == 2;

    auto inside_source = mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0},
        {0.000005, 0, 0}, {3, 0, 0}, {3, 1, 0}}, {{0, 1, 2}, {3, 4, 5}});
    geometry::polyhedron3 inside_output;
    const bool inside_tolerance = run_oracle(inside_source, inside_output, true)
        && inside_output.size_of_facets() == 2
        && inside_output.size_of_vertices() == 5;

    auto outside_source = mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0},
        {0.00002, 0, 0}, {3, 0, 0}, {3, 1, 0}}, {{0, 1, 2}, {3, 4, 5}});
    geometry::polyhedron3 outside_output;
    const bool outside_tolerance = run_oracle(outside_source, outside_output, true)
        && outside_output.size_of_facets() == 2
        && outside_output.size_of_vertices() == 6;

    auto coplanar_same_labels = mesh(
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
        {{0, 1, 2}, {0, 2, 3}});
    for (auto face = coplanar_same_labels.facets_begin();
         face != coplanar_same_labels.facets_end(); ++face)
        face->data.label = 77;
    using FaceOracle = merge_faces<geometry::Kernel, geometry::arrangement2,
        geometry::polyhedron3>;
    FaceOracle::run(coplanar_same_labels, true);
    const bool merge_faces_same_label = coplanar_same_labels.is_valid(false, 0)
        && coplanar_same_labels.size_of_facets() == 1;

    auto coplanar_different_labels = mesh(
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
        {{0, 1, 2}, {0, 2, 3}});
    auto different_face = coplanar_different_labels.facets_begin();
    different_face->data.label = 77;
    (++different_face)->data.label = 78;
    FaceOracle::run(coplanar_different_labels, true);
    const bool preserve_faces_different_labels =
        coplanar_different_labels.is_valid(false, 0)
        && coplanar_different_labels.size_of_facets() == 2;

    auto coplanar_ignore_labels = mesh(
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
        {{0, 1, 2}, {0, 2, 3}});
    auto ignored_label_face = coplanar_ignore_labels.facets_begin();
    ignored_label_face->data.label = 77;
    (++ignored_label_face)->data.label = 78;
    FaceOracle::run(coplanar_ignore_labels, false);
    const bool merge_faces_ignoring_labels =
        coplanar_ignore_labels.is_valid(false, 0)
        && coplanar_ignore_labels.size_of_facets() == 1;
    const bool first_traversed_label_survives = merge_faces_ignoring_labels
        && coplanar_ignore_labels.facets_begin()->data.label == 77;

    auto collinear_compatible = mesh(
        {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}},
        {{0, 1, 2, 3, 4}});
    set_boundary_labels(collinear_compatible, 91, 92);
    const auto compatible_cleanup = cleanup_request();
    CleanupOracle::run(collinear_compatible, compatible_cleanup);
    const bool compatible_collinear_removed =
        collinear_compatible.is_valid(false, 0)
        && collinear_compatible.size_of_vertices() == 4
        && collinear_compatible.size_of_facets() == 1
        && boundary_labels_are(collinear_compatible, 91, 92);

    auto collinear_opposite_mismatch = mesh(
        {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}},
        {{0, 1, 2, 3, 4}});
    set_boundary_labels(collinear_opposite_mismatch, 91, 92);
    auto mismatch_edge = collinear_opposite_mismatch.facets_begin()->facet_begin();
    const auto mismatch_end = mismatch_edge;
    int opposite_label = 92;
    do {
        mismatch_edge->opposite()->data.label = opposite_label++;
        ++mismatch_edge;
    } while (mismatch_edge != mismatch_end);
    const auto mismatch_cleanup = cleanup_request();
    CleanupOracle::run(collinear_opposite_mismatch, mismatch_cleanup);
    const bool opposite_mismatch_preserved =
        collinear_opposite_mismatch.is_valid(false, 0)
        && collinear_opposite_mismatch.size_of_vertices() == 5
        && collinear_opposite_mismatch.size_of_facets() == 1;

    auto collinear_current_mismatch = mesh(
        {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}},
        {{0, 1, 2, 3, 4}});
    set_boundary_labels(collinear_current_mismatch, 91, 92);
    auto current_edge = collinear_current_mismatch.facets_begin()->facet_begin();
    const auto current_end = current_edge;
    int current_label = 91;
    do {
        current_edge->data.label = current_label++;
        ++current_edge;
    } while (current_edge != current_end);
    const auto current_cleanup = cleanup_request();
    CleanupOracle::run(collinear_current_mismatch, current_cleanup);
    const bool current_mismatch_preserved =
        collinear_current_mismatch.is_valid(false, 0)
        && collinear_current_mismatch.size_of_vertices() == 5
        && collinear_current_mismatch.size_of_facets() == 1;

    auto composed_source = mesh(
        {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}},
        {{0, 1, 2}, {0, 2, 3, 4}});
    for (auto face = composed_source.facets_begin();
         face != composed_source.facets_end(); ++face) face->data.label = 77;
    set_boundary_labels(composed_source, 91, 92);
    phoenix::merge::Options composed_options;
    composed_options.merge_borders = true;
    composed_options.join_vertices = true;
    composed_options.merge_faces = true;
    composed_options.merge_faces_labels = true;
    composed_options.join_collinear = true;
    int composed_vertex_id = 1000;
    int composed_edge_id = 2000;
    auto composed = phoenix::merge::run_production_pipeline(composed_source,
        composed_options,
        [&] { return composed_vertex_id++; }, [&] { return composed_edge_id++; });
    const bool composed_pipeline = composed.success()
        && composed.mesh.is_valid(false, 0)
        && composed.mesh.size_of_facets() == 1
        && composed.mesh.size_of_vertices() == 4;

    phoenix::merge::Options invalid_options;
    auto rejected = phoenix::merge::run_production_pipeline(composed_source,
        invalid_options,
        [&] { return composed_vertex_id++; }, [&] { return composed_edge_id++; });
    const bool empty_option_set_rejected = !rejected.success()
        && rejected.mesh.empty()
        && composed_source.size_of_facets() == 2
        && composed_source.size_of_vertices() == 5;
    std::cout << "production merge empty boundary no-op: "
              << empty_is_noop << '\n'
              << "production merge disconnected faces: " << disconnected << '\n'
              << "production merge directed labels: " << labels << '\n'
              << "production merge exact shared border: " << shared << '\n'
              << "production merge duplicate reference ran: " << duplicate_ran << '\n'
              << "production merge duplicate reference faces: "
              << duplicate_faces << '\n'
              << "production merge weld inside tolerance: "
              << inside_tolerance << '\n'
              << "production merge preserve outside tolerance: "
              << outside_tolerance << '\n'
              << "production merge coplanar same-label faces: "
              << merge_faces_same_label << '\n'
              << "production merge preserve different-label faces: "
              << preserve_faces_different_labels << '\n'
              << "production merge different labels when unconstrained: "
              << merge_faces_ignoring_labels << '\n'
              << "production merge first traversed label survives: "
              << first_traversed_label_survives << '\n'
              << "production merge compatible collinear vertex removed: "
              << compatible_collinear_removed << '\n'
              << "production merge opposite-label mismatch preserved: "
              << opposite_mismatch_preserved << '\n'
              << "production merge current-label mismatch preserved: "
              << current_mismatch_preserved << '\n'
              << "production merge composed option pipeline: "
              << composed_pipeline << '\n'
              << "production merge empty option set transactional rejection: "
              << empty_option_set_rejected << '\n';
    return empty_is_noop && disconnected && labels && shared
        && duplicate_references_are_repeated && inside_tolerance
        && outside_tolerance && merge_faces_same_label
        && preserve_faces_different_labels && merge_faces_ignoring_labels
        && first_traversed_label_survives && compatible_collinear_removed
        && opposite_mismatch_preserved && current_mismatch_preserved
        && composed_pipeline && empty_option_set_rejected
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
