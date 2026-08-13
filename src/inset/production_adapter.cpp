#include "phoenix/inset/production_adapter.hpp"

#include "phoenix/inset/ported/production/inset.h"
#include "phoenix/partition/working_face.hpp"
#include "phoenix/working_geometry_builder.hpp"

#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace phoenix::inset {

GeometryItemEffect ProductionInsetResult::publication_effect(
    std::uint64_t item_key, const ActorId& owner_actor_id) const
{
    GeometryItemEffect effect;
    effect.item_key = item_key;
    effect.succeeded = success();
    effect.generated_geometry = geometry;
    effect.failure_message = error;
    if (success()) effect.consumed_faces.push_back({owner_actor_id, consumed_face_id});
    return effect;
}

namespace {

using ExactPoint = geometry::point2;
using ExactFace = geometry::face2;
using ExactHalfedge = geometry::edge2;

struct PointLess {
    bool operator()(const ExactPoint& left, const ExactPoint& right) const
    {
        return CGAL::compare_xy(left, right) == CGAL::SMALLER;
    }
};

struct WorkingInset {
    partition::PlanarFrame frame;
    geometry::arrangement2 arrangement;
    ExactFace source;
};

bool build_working_inset(const partition::ExactWorkingFace& projected,
    WorkingInset& output, std::string& error)
{
    if (projected.boundary.size() < 3) {
        error = "Inset source face requires at least three boundary edges.";
        return false;
    }
    output.frame = projected.frame;
    for (std::size_t index = 0; index < projected.boundary.size(); ++index) {
        const auto next = (index + 1) % projected.boundary.size();
        auto edge = CGAL::insert_non_intersecting_curve(output.arrangement,
            geometry::segment2{projected.boundary[index].point,
                projected.boundary[next].point});
        if (edge->source()->point() != projected.boundary[index].point)
            edge = edge->twin();
        edge->source()->data().id = static_cast<int>(
            projected.boundary[index].source_vertex_id.value());
        edge->data().id = static_cast<int>(
            projected.boundary[index].source_halfedge_id.value());
        edge->data().label = projected.boundary[index].current_label.value();
        edge->twin()->data().id = projected.boundary[index].source_opposite_halfedge_id
            ? static_cast<int>(projected.boundary[index]
                .source_opposite_halfedge_id->value())
            : -1;
        edge->twin()->data().label = projected.boundary[index].opposite_label.value();
    }
    for (auto face = output.arrangement.faces_begin();
         face != output.arrangement.faces_end(); ++face) {
        if (face->is_unbounded()) {
            face->data().label = LABEL_UNBOUNDED_IDX;
        } else if (output.source != ExactFace{}) {
            error = "Inset source boundary produced multiple bounded faces.";
            return false;
        } else {
            output.source = face;
        }
    }
    if (output.source == ExactFace{}) {
        error = "Inset source boundary did not produce a bounded face.";
        return false;
    }
    output.source->data().id = static_cast<int>(projected.source_face_id.value());
    output.source->data().label = projected.source_face_label.value();
    return true;
}

CanonicalGeometryRef publish(const partition::PlanarFrame& frame,
    const geometry::face2_list& faces, RunElementIdAllocator& ids,
    std::string& error)
{
    WorkingGeometryBuilder builder(ids);
    std::map<ExactPoint, WorkingVertexIndex, PointLess> vertices;
    std::set<const void*> emitted;
    for (const auto face : faces) {
        if (face == ExactFace{} || face->is_unbounded()
            || !emitted.insert(&*face).second) continue;
        std::vector<std::pair<ExactHalfedge, WorkingVertexIndex>> loop;
        auto edge = face->outer_ccb();
        const auto begin = edge;
        do {
            const auto point = edge->source()->point();
            auto found = vertices.find(point);
            if (found == vertices.end()) {
                found = vertices.emplace(point,
                    builder.add_vertex(frame.lift(point))).first;
            }
            loop.emplace_back(edge, found->second);
            ++edge;
        } while (edge != begin);
        if (loop.size() < 3) continue;
        const auto output_face = builder.begin_facet();
        for (const auto& item : loop) builder.add_vertex_to_facet(item.second);
        if (!builder.end_facet()) {
            error = "Inset produced an invalid face boundary.";
            return nullptr;
        }
        builder.set_face_label(output_face, LabelId{face->data().label});
        builder.set_face_tag(output_face, face->data().tag);
        for (std::size_t index = 0; index < loop.size(); ++index) {
            builder.set_halfedge_label_by_target(output_face,
                loop[(index + 1) % loop.size()].second,
                LabelId{loop[index].first->data().label});
        }
    }
    const auto built = builder.build();
    if (!built.success) {
        error = built.diagnostics.empty() ? "Inset output publication failed."
                                          : built.diagnostics.front().message;
        return nullptr;
    }
    const auto demoted = SurfaceMeshAdapter{}.demote(built.working);
    if (!demoted.success()) {
        error = demoted.diagnostics.empty() ? "Inset output demotion failed."
                                            : demoted.diagnostics.front().message;
        return nullptr;
    }
    return demoted.geometry;
}

} // namespace

ProductionInsetResult run_production_inset(
    const ProductionInsetRequest& request, RunElementIdAllocator& ids)
{
    ProductionInsetResult result;
    if (!request.source.valid()) {
        result.error = "Inset source face reference is invalid.";
        return result;
    }
    if (!std::isfinite(request.amount) || request.amount <= 0.0) {
        result.error = "Inset amount must be finite and greater than zero.";
        return result;
    }
    const auto projected = partition::ExactFaceProjector{}.project(
        *request.source.geometry, request.source.face_index);
    if (!projected.success()) {
        result.error = projected.error;
        return result;
    }
    WorkingInset working;
    if (!build_working_inset(projected.face, working, result.error)) return result;

    geometry::face2_list center_faces;
    geometry::face2_list side_faces;
    inset_request kernel_request(request.amount, working.arrangement,
        working.source, &center_faces, &side_faces);
    kernel_request.labels.result_face = request.labels.result_face.value();
    kernel_request.labels.side_face = request.labels.side_face.value();
    kernel_request.labels.result_edge = request.labels.result_edge.value();
    kernel_request.labels.left_edge = request.labels.left_edge.value();
    kernel_request.labels.right_edge = request.labels.right_edge.value();
    kernel_request.labels.top_edge = request.labels.top_edge.value();
    kernel_request.labels.bottom_edge = request.labels.bottom_edge.value();
    if (!::inset::run(kernel_request, nullptr, false)) {
        result.error = "Production inset kernel failed.";
        return result;
    }
    center_faces.insert(center_faces.end(), side_faces.begin(), side_faces.end());
    result.geometry = publish(working.frame, center_faces, ids, result.error);
    if (result.geometry) result.consumed_face_id = projected.face.source_face_id;
    return result;
}

} // namespace phoenix::inset
