#include "phoenix/extrusion/profile.hpp"
#include "phoenix/working_geometry.hpp"

#include <cstdlib>
#include <iostream>

int main()
{
    const auto profile = phoenix::extrusion::Profile::create({
        {2.0, 0.0, phoenix::LabelId{1}, phoenix::LabelId{2}, phoenix::LabelId{3},
            phoenix::LabelId{4}, phoenix::LabelId{5}, phoenix::LabelId{6}, true},
        {0.0, 3.0, phoenix::LabelId{7}, phoenix::LabelId{8}, phoenix::LabelId{9},
            phoenix::LabelId{10}, phoenix::LabelId{11}, phoenix::LabelId{12}, false},
    });
    phoenix::ExtrusionWorkingFace face;
    face.source_face_id = phoenix::FaceId{20};
    face.face_label = phoenix::LabelId{21};
    face.boundary = {
        {{0, 0, 0}, phoenix::VertexId{1}, phoenix::HalfedgeId{4}, phoenix::EdgeId{7}, phoenix::LabelId{10}},
        {{1, 0, 0}, phoenix::VertexId{2}, phoenix::HalfedgeId{5}, phoenix::EdgeId{8}, phoenix::LabelId{11}},
        {{0, 0, 1}, phoenix::VertexId{3}, phoenix::HalfedgeId{6}, phoenix::EdgeId{9}, phoenix::LabelId{12}},
    };
    const auto input = phoenix::extrusion::make_kernel_input(
        face, profile, phoenix::LabelId{30}, phoenix::LabelId{31}, phoenix::LabelId{32},
        phoenix::LabelId{33}, phoenix::LabelId{34}, phoenix::LabelId{35});
    const auto direction = profile->direction(1);
    const auto horizontal_only = phoenix::extrusion::Profile::create({
        {1.0, 0.0, phoenix::LabelId{40}, phoenix::LabelId{41},
            phoenix::LabelId{42}, phoenix::LabelId{43}, phoenix::LabelId{44},
            phoenix::LabelId{45}, true}}, CGAL::POSITIVE);
    const auto horizontal_input = phoenix::extrusion::make_kernel_input(
        face, horizontal_only, phoenix::LabelId{30}, phoenix::LabelId{31},
        phoenix::LabelId{32}, phoenix::LabelId{33}, phoenix::LabelId{34},
        phoenix::LabelId{35});
    const bool ok = profile && profile->size() == 2 && profile->sign() == CGAL::POSITIVE
        && direction.first == 0.0 && direction.second == 3.0
        && profile->segment(0).horizontal
        && profile->segment(1).skirt_label == phoenix::LabelId{12}
        && input && input->boundary.size() == 3
        && input->boundary[0].source_vertex_id == phoenix::VertexId{1}
        && input->boundary[0].source_edge_id == phoenix::EdgeId{7}
        && input->boundary[0].cap_label == phoenix::LabelId{35}
        && horizontal_only && horizontal_only->sign() == CGAL::POSITIVE
        && horizontal_input && horizontal_input->sign == CGAL::POSITIVE;
    if (!ok) {
        std::cerr << "extrusion profile tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "extrusion profile tests passed\n";
    return EXIT_SUCCESS;
}
