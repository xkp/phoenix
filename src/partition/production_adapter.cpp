#include "phoenix/partition/production_adapter.hpp"

#include "phoenix/partition/ported/production/partition_solver.h"
#include "phoenix/partition/ported/production/partition_tesselator.h"
#include "phoenix/partition/working_face.hpp"
#include "phoenix/working_geometry_builder.hpp"

#include <CGAL/Aos_observer.h>

#include <map>
#include <memory>
#include <set>
#include <utility>

namespace phoenix::partition {

GeometryItemEffect ProductionPartitionResult::publication_effect(
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

using ProductionArrangement = geometry::arrangement2;
using ProductionFace = geometry::face2;
using ProductionHalfedge = geometry::edge2;
using ProductionPoint = geometry::point2;

struct PointLess {
    bool operator()(const ProductionPoint& left, const ProductionPoint& right) const
    {
        return CGAL::compare_xy(left, right) == CGAL::SMALLER;
    }
};

struct ProductionInput {
    PlanarFrame frame;
    ProductionArrangement arrangement;
    ProductionFace face;
};

class LabelMatcher final : public match_edge2 {
public:
    explicit LabelMatcher(LabelId label) : label_(label.value()) {}
    void build(vm::icontext_ref) override {}
    bool match(edge2 halfedge) override { return halfedge->data().label == label_; }

private:
    int label_;
};

bool build_input(const ExactWorkingFace& source, ProductionInput& output,
    std::string& error)
{
    if (source.boundary.size() < 3) {
        error = "Partition source face requires at least three boundary edges.";
        return false;
    }

    output.frame = source.frame;
    for (std::size_t index = 0; index < source.boundary.size(); ++index) {
        const auto next = (index + 1) % source.boundary.size();
        auto halfedge = CGAL::insert_non_intersecting_curve(output.arrangement,
            geometry::segment2{source.boundary[index].point,
                source.boundary[next].point});
        if (halfedge->source()->point() != source.boundary[index].point)
            halfedge = halfedge->twin();

        halfedge->source()->data().id =
            static_cast<int>(source.boundary[index].source_vertex_id.value());
        halfedge->data().id =
            static_cast<int>(source.boundary[index].source_halfedge_id.value());
        halfedge->data().label = source.boundary[index].current_label.value();
        halfedge->twin()->data().id = source.boundary[index].source_opposite_halfedge_id
            ? static_cast<int>(source.boundary[index].source_opposite_halfedge_id->value())
            : -1;
        halfedge->twin()->data().label = source.boundary[index].opposite_label.value();
    }

    for (auto face = output.arrangement.faces_begin();
         face != output.arrangement.faces_end(); ++face) {
        if (face->is_unbounded()) {
            face->data().label = LABEL_UNBOUNDED_IDX;
            continue;
        }
        if (output.face != ProductionFace{}) {
            error = "Partition source boundary produced multiple bounded faces.";
            return false;
        }
        output.face = face;
    }
    if (output.face == ProductionFace{}) {
        error = "Partition source boundary did not produce a bounded face.";
        return false;
    }
    output.face->data().id = static_cast<int>(source.source_face_id.value());
    output.face->data().label = source.source_face_label.value();
    return true;
}

CanonicalGeometryRef publish_faces(const PlanarFrame& frame,
    const geometry::face2_list& source_faces, RunElementIdAllocator& ids,
    std::string& error)
{
    WorkingGeometryBuilder builder(ids);
    std::map<ProductionPoint, WorkingVertexIndex, PointLess> vertices;
    std::set<const void*> emitted;

    for (const auto face : source_faces) {
        if (face == ProductionFace{} || face->is_unbounded()
            || !emitted.insert(&*face).second)
            continue;

        std::vector<std::pair<ProductionHalfedge, WorkingVertexIndex>> loop;
        auto edge = face->outer_ccb();
        const auto begin = edge;
        do {
            const auto point = edge->source()->point();
            auto found = vertices.find(point);
            if (found == vertices.end()) {
                found = vertices.emplace(point, builder.add_vertex(frame.lift(point))).first;
            }
            loop.emplace_back(edge, found->second);
            ++edge;
        } while (edge != begin);

        if (loop.size() < 3) continue;
        const auto output_face = builder.begin_facet();
        for (const auto& item : loop) builder.add_vertex_to_facet(item.second);
        if (!builder.end_facet()) {
            error = "Partition produced an invalid face boundary.";
            return nullptr;
        }
        builder.set_face_label(output_face, LabelId{face->data().label});
        for (std::size_t index = 0; index < loop.size(); ++index) {
            const auto target = loop[(index + 1) % loop.size()].second;
            builder.set_halfedge_label_by_target(
                output_face, target, LabelId{loop[index].first->data().label});
        }
    }

    const auto built = builder.build();
    if (!built.success) {
        error = built.diagnostics.empty()
            ? "Partition output publication failed."
            : built.diagnostics.front().message;
        return nullptr;
    }
    return SurfaceMeshAdapter{}.demote(built.working).geometry;
}

} // namespace

ProductionPartitionResult run_production_partition(
    const ProductionPartitionRequest& request, RunElementIdAllocator& ids)
{
    ProductionPartitionResult result;
    if (!request.source.valid()) {
        result.error = "Partition source face reference is invalid.";
        return result;
    }
    if (request.model == nullptr) {
        result.error = "Partition requires a linked production model.";
        return result;
    }

    const auto projected = ExactFaceProjector{}.project(
        *request.source.geometry, request.source.face_index);
    if (!projected.success()) {
        result.error = projected.error;
        return result;
    }

    ProductionInput input;
    if (!build_input(projected.face, input, result.error)) return result;

    auto context = std::make_shared<vm::icontext>();
    context->values.insert(request.values.begin(), request.values.end());
    auto random = randomizer_factory::get_fixed(request.random_seed);
    auto repository = std::make_shared<segment_repository>();
    std::map<repo_segment_id, std::unique_ptr<LabelMatcher>> owned_matchers;
    std::map<repo_segment_id, match_edge2*> conditions;
    for (const auto& item : request.base_segment_labels) {
        const repo_segment_id id(item.first);
        auto matcher = std::make_unique<LabelMatcher>(item.second);
        conditions.emplace(id, matcher.get());
        owned_matchers.emplace(id, std::move(matcher));
    }
    repository->search(input.face, conditions);
    repository->randomize(random);

    partition_view view(context, repository, random);
    view.notify = std::make_shared<partition_notify>();
    auto& plan = request.model->create_plan(context);
    using ProductionSolver = solver<partition_model, partition_view, partition_plan>;
    ProductionSolver::context solver_context;
    if (!ProductionSolver::run(*request.model, plan, view, &solver_context)) {
        result.error = "Production partition solver found no valid solution.";
        return result;
    }

    geometry::face2_list output_faces;
    partition_tesselator tessellator(
        context, input.arrangement, input.face, *request.model, view, random);
    tessellator.run(output_faces);
    result.geometry = publish_faces(projected.face.frame, output_faces, ids, result.error);
    if (result.geometry) result.consumed_face_id = projected.face.source_face_id;
    return result;
}

} // namespace phoenix::partition
