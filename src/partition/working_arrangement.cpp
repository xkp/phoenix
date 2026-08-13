#include "phoenix/partition/working_arrangement.hpp"

namespace phoenix::partition {

ArrangementBuildResult ExactArrangementBuilder::build(const ExactWorkingFace& face) const
{
    ArrangementBuildResult result;
    if (face.boundary.size() < 3) {
        result.error = "partition arrangement requires at least three boundary edges";
        return result;
    }

    WorkingArrangement working;
    working.frame = face.frame;
    for (std::size_t index = 0; index < face.boundary.size(); ++index) {
        const auto next = (index + 1) % face.boundary.size();
        const ExactArrangementTraits::X_monotone_curve_2 curve{
            face.boundary[index].point, face.boundary[next].point};
        auto halfedge = CGAL::insert_non_intersecting_curve(working.arrangement, curve);
        if (halfedge->source()->point() != face.boundary[index].point)
            halfedge = halfedge->twin();

        halfedge->source()->set_data({-1, static_cast<std::int64_t>(index), -1,
            face.boundary[index].source_vertex_id});
        halfedge->set_data({-1, -1, face.boundary[index].source_halfedge_id,
            face.boundary[index].source_edge_id,
            face.boundary[index].current_label,
            face.boundary[index].opposite_face_label});
        halfedge->twin()->set_data({-1, -1,
            face.boundary[index].source_opposite_halfedge_id,
            face.boundary[index].source_edge_id,
            face.boundary[index].opposite_label,
            face.source_face_label});
    }

    ExactArrangement::Face_handle bounded_face;
    for (auto face_it = working.arrangement.faces_begin();
         face_it != working.arrangement.faces_end(); ++face_it) {
        if (!face_it->is_unbounded()) {
            if (bounded_face != ExactArrangement::Face_handle{}) {
                result.error = "partition input boundary produced multiple bounded faces";
                return result;
            }
            bounded_face = face_it;
        }
    }
    if (bounded_face == ExactArrangement::Face_handle{}) {
        result.error = "partition input boundary did not produce a bounded face";
        return result;
    }
    bounded_face->set_data({-1, -1, face.source_face_id, face.source_face_label});
    result.working.emplace(std::move(working));
    return result;
}

} // namespace phoenix::partition
