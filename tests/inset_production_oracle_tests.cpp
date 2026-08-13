#include "phoenix/inset/ported/production/inset.h"
#include <cstdlib>
#include <iostream>
#include <set>

int main()
{
    geometry::arrangement2 arrangement;
    const std::vector<geometry::point2> points{
        {0, 0}, {6, 0}, {6, 4}, {0, 4}};
    for (std::size_t index = 0; index < points.size(); ++index) {
        auto edge = CGAL::insert_non_intersecting_curve(arrangement,
            geometry::segment2{points[index], points[(index + 1) % points.size()]});
        if (edge->source()->point() != points[index]) edge = edge->twin();
        edge->data().id = static_cast<int>(100 + index);
        edge->data().label = static_cast<int>(30 + index);
        edge->twin()->data().label = static_cast<int>(40 + index);
    }

    geometry::face2 source;
    for (auto face = arrangement.faces_begin(); face != arrangement.faces_end(); ++face) {
        if (face->is_unbounded()) face->data().label = LABEL_UNBOUNDED_IDX;
        else source = face;
    }
    source->data().id = 50;
    source->data().label = 51;

    geometry::face2_list results;
    geometry::face2_list sides;
    inset_request request(1.0, arrangement, source, &results, &sides);
    request.labels.result_face = 60;
    request.labels.side_face = 61;
    request.labels.result_edge = 62;
    request.labels.left_edge = 63;
    request.labels.right_edge = 64;
    request.labels.top_edge = 65;
    request.labels.bottom_edge = 66;

    const bool ran = inset::run(request, nullptr, false);
    std::set<int> labels;
    for (auto face = arrangement.faces_begin(); face != arrangement.faces_end(); ++face)
        if (!face->is_unbounded()) labels.insert(face->data().label);
    const bool topology = ran && results.size() == 1 && sides.size() == 4
        && arrangement.number_of_faces() == 6;
    const bool face_labels = labels == std::set<int>{60, 61};

    std::cout << "production inset rectangle topology: " << topology << '\n'
              << "production inset rectangle labels: " << face_labels << '\n';
    return topology && face_labels ? EXIT_SUCCESS : EXIT_FAILURE;
}
