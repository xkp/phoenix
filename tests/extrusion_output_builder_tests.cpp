#include "phoenix/extrusion/output_builder.hpp"

#include <cstdlib>
#include <iostream>

namespace {

bool test_metadata_and_demotion()
{
    phoenix::RunElementIdAllocator ids{1000};
    phoenix::extrusion::OutputAdapter builder{ids};
    const auto a = builder.add_vertex({0, 0, 0});
    const auto b = builder.add_vertex({1, 0, 0});
    const auto c = builder.add_vertex({0, 1, 0});
    builder.set_vertex_id(a, phoenix::VertexId{10});

    const auto face = builder.begin_facet();
    if (face == phoenix::extrusion::invalid_output_face_index
        || !builder.add_vertex_to_facet(a)
        || !builder.add_vertex_to_facet(b)
        || !builder.add_vertex_to_facet(c)
        || !builder.end_facet()) return false;
    builder.set_face_label(face, phoenix::LabelId{41});
    builder.set_face_tag(face, phoenix::extrusion::cap_face_tag);
    builder.set_halfedge_label_by_target(face, a, phoenix::LabelId{51});
    builder.set_halfedge_label_by_target(face, b, phoenix::LabelId{52});
    builder.set_halfedge_label_by_target(face, c, phoenix::LabelId{53});

    const auto built = builder.build();
    if (!built.success || built.working.mesh.number_of_faces() != 1) return false;
    const auto mesh_face = *built.working.mesh.faces().begin();
    if (built.working.face_labels[mesh_face] != 41
        || built.working.face_tags[mesh_face] != phoenix::extrusion::cap_face_tag) return false;

    auto halfedge = built.working.mesh.halfedge(mesh_face);
    do {
        const auto target = built.working.mesh.target(halfedge).idx();
        const auto expected = target == a ? 51 : target == b ? 52 : 53;
        if (built.working.halfedge_labels[halfedge] != expected) return false;
        halfedge = built.working.mesh.next(halfedge);
    } while (halfedge != built.working.mesh.halfedge(mesh_face));

    const auto demoted = phoenix::SurfaceMeshAdapter{}.demote(built.working);
    return demoted.success()
        && demoted.geometry->vertices()[a].id == phoenix::VertexId{10}
        && demoted.geometry->faces()[0].label == phoenix::LabelId{41};
}

bool test_open_facet_is_rejected()
{
    phoenix::RunElementIdAllocator ids;
    phoenix::extrusion::OutputAdapter builder{ids};
    const auto a = builder.add_vertex({0, 0, 0});
    const auto face = builder.begin_facet();
    return face != phoenix::extrusion::invalid_output_face_index
        && builder.add_vertex_to_facet(a)
        && !builder.build().success;
}

bool test_nested_and_short_facets_are_rejected()
{
    phoenix::RunElementIdAllocator ids;
    phoenix::extrusion::OutputAdapter builder{ids};
    const auto a = builder.add_vertex({0, 0, 0});
    const auto first = builder.begin_facet();
    const auto nested = builder.begin_facet();
    return first != phoenix::extrusion::invalid_output_face_index
        && nested == phoenix::extrusion::invalid_output_face_index
        && builder.add_vertex_to_facet(a)
        && !builder.end_facet();
}

} // namespace

int main()
{
    const bool metadata = test_metadata_and_demotion();
    const bool open = test_open_facet_is_rejected();
    const bool state = test_nested_and_short_facets_are_rejected();
    std::cout << "metadata and demotion: " << metadata << '\n'
              << "open facet rejection: " << open << '\n'
              << "builder state rejection: " << state << '\n';
    if (!(metadata && open && state)) return EXIT_FAILURE;
    std::cout << "extrusion output builder tests passed\n";
    return EXIT_SUCCESS;
}
