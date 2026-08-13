#include "phoenix/partition/working_face.hpp"
#include "phoenix/partition/working_arrangement.hpp"
#include "phoenix/partition/compat/geometry_types.hpp"
#include "phoenix/partition/adapted_constraints.hpp"
#include "phoenix/partition/ported/partition_solver_filters.h"
#include "phoenix/partition/ported/partition_solver_foundation.h"
#include "phoenix/partition/ported/null_diagnostics.hpp"
#include "phoenix/partition/plan_executor.hpp"
#include "phoenix/partition/straight_cut_tessellator.hpp"
#include "phoenix/partition/arrangement_segment_repository.hpp"
#include "phoenix/partition/sampling.hpp"
#include "phoenix/partition/trusted_solver_foundation.hpp"
#include "phoenix/partition/trusted_branching.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <type_traits>

static_assert(std::is_same_v<phoenix::partition::compat::Kernel,
    phoenix::partition::ExactKernel>);
static_assert(std::is_same_v<phoenix::partition::compat::arrangement2,
    phoenix::partition::ExactArrangement>);
static_assert(std::is_same_v<phoenix::partition::compat::edge2,
    phoenix::partition::ExactArrangement::Halfedge_handle>);
static_assert(std::is_same_v<debug_json, null_partition_diagnostics>);

namespace {

phoenix::CanonicalGeometryRef tilted_rectangle()
{
    std::vector<phoenix::RuntimeVertex> vertices{
        {{1, 2, 3}, phoenix::VertexId{1}, 0},
        {{5, 2, 3}, phoenix::VertexId{2}, 1},
        {{5, 5, 6}, phoenix::VertexId{3}, 2},
        {{1, 5, 6}, phoenix::VertexId{4}, 3},
    };
    std::vector<phoenix::RuntimeHalfedge> halfedges{
        {0, 0, 1, 3, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{10}, phoenix::EdgeId{20}, phoenix::LabelId{30}},
        {1, 0, 2, 0, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{11}, phoenix::EdgeId{21}, phoenix::LabelId{31}},
        {2, 0, 3, 1, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{12}, phoenix::EdgeId{22}, phoenix::LabelId{32}},
        {3, 0, 0, 2, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{13}, phoenix::EdgeId{23}, phoenix::LabelId{33}},
    };
    return phoenix::CanonicalGeometry::create(
        std::move(vertices), std::move(halfedges),
        {{0, phoenix::FaceId{40}, phoenix::LabelId{41}}});
}

bool close(phoenix::Point3d left, phoenix::Point3d right)
{
    return std::abs(left.x - right.x) < 1e-9
        && std::abs(left.y - right.y) < 1e-9
        && std::abs(left.z - right.z) < 1e-9;
}

phoenix::partition::ExactWorkingFace split_rectangle()
{
    using namespace phoenix;
    using namespace phoenix::partition;
    ExactWorkingFace face;
    face.source_face_id = FaceId{50};
    face.source_face_label = LabelId{51};
    const std::vector<ExactPoint2> points{{0, 0}, {2, 0}, {4, 0}, {4, 3}, {0, 3}};
    const std::vector<LabelId> labels{
        LabelId{60}, LabelId{60}, LabelId{61}, LabelId{62}, LabelId{63}};
    for (std::size_t index = 0; index < points.size(); ++index) {
        face.boundary.push_back({points[index], VertexId{100 + index},
            HalfedgeId{200 + index}, EdgeId{300 + index}, labels[index],
            LabelId{70 + static_cast<std::int32_t>(index)}, LabelId{80},
            std::nullopt});
    }
    return face;
}

} // namespace

int main()
{
    const auto geometry = tilted_rectangle();
    const auto projected = phoenix::partition::ExactFaceProjector{}.project(*geometry, 0);
    bool ok = projected.success() && projected.face.boundary.size() == 4
        && projected.face.source_face_id == phoenix::FaceId{40}
        && projected.face.source_face_label == phoenix::LabelId{41};
    for (std::size_t i = 0; ok && i < projected.face.boundary.size(); ++i) {
        const auto label_offset = static_cast<std::int32_t>(i);
        const auto edge_offset = static_cast<std::uint64_t>(i);
        ok = projected.face.boundary[i].current_label == phoenix::LabelId{30 + label_offset}
            && projected.face.boundary[i].source_edge_id == phoenix::EdgeId{20 + edge_offset}
            && close(projected.face.frame.lift(projected.face.boundary[i].point),
                geometry->vertices()[i].point);
    }
    std::cout << "exact planar 3D projection/lifting: " << ok << '\n';
    auto arrangement = phoenix::partition::ExactArrangementBuilder{}.build(projected.face);
    ok = ok && arrangement.success()
        && arrangement.working->arrangement.number_of_edges() == 4
        && arrangement.working->arrangement.number_of_faces() == 2;
    if (ok) {
        for (auto face = arrangement.working->arrangement.faces_begin();
             face != arrangement.working->arrangement.faces_end(); ++face) {
            if (face->is_unbounded()) continue;
            ok = face->data().source_face_id == phoenix::FaceId{40}
                && face->data().label == phoenix::LabelId{41};
            auto edge = face->outer_ccb();
            const auto end = edge;
            do {
                ok = ok && edge->data().source_edge_id.has_value()
                    && edge->data().label != phoenix::unassigned_label_id;
                ++edge;
            } while (edge != end);
        }
    }
    phoenix::partition::ExactArrangement::Face_handle bounded_face;
    for (auto face = arrangement.working->arrangement.faces_begin();
         face != arrangement.working->arrangement.faces_end(); ++face)
        if (!face->is_unbounded()) bounded_face = face;
    std::vector<phoenix::partition::BaseSegmentDefinition> conditions;
    for (std::int32_t index = 0; index < 4; ++index) {
        conditions.push_back({phoenix::partition::SegmentRef{index},
            {{phoenix::partition::LabelMatchField::current,
                phoenix::partition::LabelMatchComparison::equal,
                phoenix::LabelId{30 + index}}}});
    }
    phoenix::partition::ArrangementSegmentRepository repository;
    std::string repository_error;
    ok = ok && repository.search(bounded_face, conditions, repository_error)
        && repository.size() == 4 && repository.all_edges().size() == 4
        && repository.unmatched().empty()
        && repository.get(phoenix::partition::SegmentRef{0}) != nullptr
        && repository.get(phoenix::partition::SegmentRef{0})->size() == 1;

    auto split_arrangement =
        phoenix::partition::ExactArrangementBuilder{}.build(split_rectangle());
    phoenix::partition::ExactArrangement::Face_handle split_face;
    for (auto face = split_arrangement.working->arrangement.faces_begin();
         face != split_arrangement.working->arrangement.faces_end(); ++face)
        if (!face->is_unbounded()) split_face = face;
    const std::vector<phoenix::partition::BaseSegmentDefinition> split_conditions{
        {phoenix::partition::SegmentRef{0},
            {{phoenix::partition::LabelMatchField::current,
                phoenix::partition::LabelMatchComparison::equal,
                phoenix::LabelId{60}}}},
    };
    phoenix::partition::ArrangementSegmentRepository split_repository;
    ok = ok && split_repository.search(
        split_face, split_conditions, repository_error)
        && split_repository.all_edges().size() == 4
        && split_repository.get(phoenix::partition::SegmentRef{0})->size() == 1
        && split_repository.get(phoenix::partition::SegmentRef{0})->front()
            .segment == phoenix::partition::ExactKernel::Segment_2{{0, 0}, {4, 0}};
    auto grouped = split_repository.get(phoenix::partition::SegmentRef{0})->front();
    grouped.set_label(phoenix::LabelId{90});
    ok = ok && grouped.halfedge_start->data().label == phoenix::LabelId{90}
        && grouped.halfedge_start->next()->data().label == phoenix::LabelId{90};
    phoenix::partition::CompatibilityRandomStream random_a{123};
    phoenix::partition::CompatibilityRandomStream random_b{123};
    split_repository.randomize(random_a);
    phoenix::partition::ArrangementSegmentRepository split_repository_replay;
    ok = ok && split_repository_replay.search(
        split_face, split_conditions, repository_error);
    split_repository_replay.randomize(random_b);
    ok = ok && split_repository.all_edges().front().segment
        == split_repository_replay.all_edges().front().segment;

    using namespace phoenix::partition::trusted;
    {
        using namespace phoenix::partition::ported;
        cut_segment_id source_cut{4};
        segment_info production_info{source_cut,
            phoenix::partition::ExactPoint2{0, 0},
            phoenix::partition::ExactPoint2{4, 0}};
        phoenix::partition::compat::line2 restriction_line{1, 0, -1};
        angle_range production_angle{0.0, std::acos(-1.0) / 2.0};
        phoenix::partition::CompatibilityRandomStream production_random{4321};
        partition_view production_view{split_repository, production_random};
        production_view.add_segment(production_info);
        production_view.restrict_angles(0.0, std::acos(-1.0) / 2.0,
            std::acos(-1.0) / 2.0);
        partition_view production_copy{production_view};
        production_copy.notify_error(cut_segment_id{9});
        ok = ok && source_cut.cut_result(cut_segment_type{SOURCE_LEFT}).value == 5
            && production_info.restrict_line(restriction_line)
            && production_info.src == phoenix::partition::ExactPoint2{1, 0}
            && production_angle.inside(std::acos(-1.0) / 4.0)
            && production_view.has_segment(source_cut)
            && production_view.is_angle_restricted()
            && production_copy.has_errors
            && production_copy.first_error->value == 9
            && !production_view.has_errors;
    }
    phoenix::partition::CompatibilityRandomStream trusted_random{321};
    PartitionView trusted_view{split_repository, trusted_random};
    const CutSegmentId base_id{0};
    auto* trusted_segment = trusted_view.add_segment(
        SegmentInfo{base_id,
            split_repository.get(phoenix::partition::SegmentRef{0})->front()});
    ok = ok && trusted_view.has_segment(base_id)
        && trusted_view.has_edge(trusted_segment->repository_edge)
        && base_id.cut_result(CutSegmentType{CutSegmentKind::target_right}).value == 4
        && CutSegmentType{CutSegmentKind::source_left}.is_source()
        && CutSegmentType{CutSegmentKind::target_left}.is_left();
    ok = ok && trusted_segment->restrict_line(
        phoenix::partition::ExactKernel::Line_2{1, 0, -1})
        && trusted_segment->source == phoenix::partition::ExactPoint2{1, 0};
    trusted_view.restrict_angles(0.0, std::acos(-1.0) / 2.0,
        std::acos(-1.0) / 2.0);
    ok = ok && trusted_view.is_angle_restricted()
        && trusted_view.angles.size() == 1;
    PartitionView trusted_copy{trusted_view};
    trusted_copy.notify_error(CutSegmentId{7});
    ok = ok && trusted_copy.first_error->value == 7
        && !trusted_view.has_errors;
    trusted_view.reset();
    ok = ok && trusted_view.segment(base_id)->source
        == trusted_view.segment(base_id)->original_source
        && !trusted_view.is_angle_restricted();

    phoenix::partition::CompatibilityRandomStream branch_random{456};
    PartitionView branch_view{repository, branch_random};
    BranchingModel branching{4, {}};
    ok = ok && branching.branch_simple(branch_view, CutSegmentId{0}, false) != nullptr
        && branching.branch_simple(branch_view, CutSegmentId{0}, false) == nullptr;
    PartitionView pair_view{repository, branch_random};
    PartitionViewList pair_results;
    phoenix::partition::ported::base_length_pct_filter length_filter{
        {99.0}, {101.0}};
    phoenix::partition::ported::base_angle_filter angle_filter{{89.0}, {91.0}};
    ok = ok && length_filter(repository.all_edges()[0], repository.all_edges()[2])
        && angle_filter(repository.all_edges()[0], repository.all_edges()[1]);
    ok = ok && branching.branch(pair_view, CutSegmentId{0}, CutSegmentId{2},
            pair_results, false) == BranchReturnType::ok
        && pair_results.size() == 1
        && pair_results.front().has_segment(CutSegmentId{0})
        && pair_results.front().has_segment(CutSegmentId{2})
        && pair_results.front().quality > 0.0
        && pair_results.front().cut_middle_line.has_value();
    BranchingModel filtered_branching{4, {},
        {{CutSegmentId{0}, CutSegmentId{2},
            [](const auto&, const auto&) { return false; }, false}}};
    PartitionViewList filtered_results;
    ok = ok && filtered_branching.branch(pair_view, CutSegmentId{0},
            CutSegmentId{2}, filtered_results, false)
            == BranchReturnType::fail_both
        && filtered_results.empty();
    const auto& selected_view = pair_results.front();
    const auto* selected_source = selected_view.segment(CutSegmentId{0});
    const auto* selected_target = selected_view.segment(CutSegmentId{2});
    const auto cut_source = CGAL::midpoint(
        selected_source->source, selected_source->target);
    auto cut_target = CGAL::midpoint(
        selected_target->source, selected_target->target);
    phoenix::partition::CompatibilityRandomStream angle_random{789};
    ok = ok && branching.angle_solution(angle_random, cut_source,
        std::acos(-1.0) / 2.0, std::acos(-1.0) / 2.0,
        selected_target->segment(), cut_target);
    const TrustedCut trusted_cut{0, CutSegmentId{4}, CutSegmentId{0},
        CutSegmentId{2}, std::nullopt, std::nullopt, std::nullopt};
    BranchingModel cut_branching{4, {trusted_cut}};
    auto cut_view = branching.view_for_cut(pair_results.front(), trusted_cut,
        cut_source, cut_target, 0.25);
    ok = ok && cut_view.has_segment(CutSegmentId{4})
        && cut_view.has_segment(CutSegmentId{5})
        && cut_view.has_segment(CutSegmentId{6})
        && cut_view.has_segment(CutSegmentId{7})
        && cut_view.has_segment(CutSegmentId{8})
        && cut_view.quality == 0.25;
    phoenix::partition::CompatibilityRandomStream cut_random{987};
    PartitionView production_cut_view{repository, cut_random};
    PartitionViewList selected_pairs;
    ok = ok && branching.branch(production_cut_view, CutSegmentId{0},
        CutSegmentId{2}, selected_pairs, false) == BranchReturnType::ok;
    PartitionViewList production_cut_results;
    cut_branching.branch(selected_pairs.front(), trusted_cut, production_cut_results);
    ok = ok && production_cut_results.size()
            == phoenix::partition::production_cut_variation_count;
    for (const auto& result : production_cut_results)
        ok = ok && result.has_segment(CutSegmentId{4})
            && result.has_segment(CutSegmentId{5})
            && result.has_segment(CutSegmentId{6})
            && result.has_segment(CutSegmentId{7})
            && result.has_segment(CutSegmentId{8});
    auto restricted_view = selected_pairs.front();
    std::string restriction_error;
    const phoenix::partition::adapted::RestrictCutSegmentLength length_worker{
        0, true, false, 5.0, 6.0};
    const auto length_ok = !length_worker.apply(
        restricted_view, cut_branching, restriction_error)
        && restriction_error == "segment too short";
    ok = ok && length_ok;
    PartitionViewList angle_views;
    const phoenix::partition::adapted::RestrictCutAngle angle_worker{
        CutSegmentId{0}, 90.0, 90.0};
    angle_worker.apply(pair_view, branching, angle_views);
    const auto angle_ok = angle_views.size() == 1
        && angle_views.front().is_angle_restricted();
    ok = ok && angle_ok;
    auto distance_view = selected_pairs.front();
    ok = ok && branching.branch_simple(
        distance_view, CutSegmentId{1}, false) != nullptr;
    const phoenix::partition::adapted::RestrictCutDistance distance_worker{
        CutSegmentId{4}, CutSegmentId{1}, false, 0.0, false, 0.0};
    std::string distance_error;
    const auto distance_ok = distance_worker.apply(
        distance_view, cut_branching, distance_error);
    const phoenix::partition::adapted::RestrictCutDistancePercent
        invalid_percentage_worker{CutSegmentId{4}, CutSegmentId{1},
            true, 10.0, false, 0.0, CutSegmentId{99}};
    const auto percentage_rejection_ok = !invalid_percentage_worker.apply(
        distance_view, cut_branching, distance_error);
    const phoenix::partition::adapted::RestrictDistanceExtra extra_worker{
        0, 1, true, 1.0};
    const auto extra_unrestricted_ok = extra_worker.apply(
        distance_view, cut_branching, distance_error);
    ok = ok && distance_ok && percentage_rejection_ok && extra_unrestricted_ok;
    std::cout << "production adapted length/angle constraints: "
        << length_ok << ' ' << angle_ok << '\n';
    std::cout << "production adapted distance constraints: "
        << distance_ok << ' ' << percentage_rejection_ok << ' '
        << extra_unrestricted_ok << '\n';
    const phoenix::partition::PartitionPlan execution_plan{4, {}, {},
        {{0, phoenix::partition::PlanPriority::select_edges, 0,
            phoenix::partition::SelectEdgesStep{
                phoenix::partition::SegmentRef{0},
                phoenix::partition::SegmentRef{2}}},
         {0, phoenix::partition::PlanPriority::length_constraint, 1,
            phoenix::partition::ConstraintStep{0}}}};
    phoenix::partition::adapted::LinkedPlanExecution linked_execution;
    linked_execution.constraints.push_back([](PartitionView&) { return true; });
    linked_execution.evaluators.resize(1);
    linked_execution.evaluators[0].push_back(
        [](PartitionView&) { return true; });
    phoenix::partition::adapted::PlanExecutor executor{
        execution_plan, branching, linked_execution};
    PartitionView execution_view{repository, branch_random};
    PartitionViewList execution_branches;
    const auto advance_branch = executor.advance(
        execution_view, execution_branches);
    const auto advance_continue = execution_branches.empty()
        ? phoenix::partition::adapted::AdvanceResult::fail
        : executor.advance(execution_branches.front(), execution_branches);
    const auto advance_succeed = execution_branches.empty()
        ? phoenix::partition::adapted::AdvanceResult::fail
        : executor.advance(execution_branches.front(), execution_branches);
    const auto executor_ok = advance_branch
            == phoenix::partition::adapted::AdvanceResult::branch
        && advance_continue
            == phoenix::partition::adapted::AdvanceResult::continue_execution
        && advance_succeed
            == phoenix::partition::adapted::AdvanceResult::succeed;
    ok = ok && executor_ok;
    std::cout << "production partition plan advance: " << executor_ok << '\n';
    phoenix::partition::CutLabels tessellation_labels;
    tessellation_labels.face_left = phoenix::LabelId{200};
    tessellation_labels.face_right = phoenix::LabelId{201};
    tessellation_labels.cut_left = phoenix::LabelId{202};
    tessellation_labels.cut_right = phoenix::LabelId{203};
    tessellation_labels.source_left = phoenix::LabelId{204};
    tessellation_labels.source_right = phoenix::LabelId{205};
    tessellation_labels.target_left = phoenix::LabelId{206};
    tessellation_labels.target_right = phoenix::LabelId{207};
    tessellation_labels.source_left_opposite = phoenix::LabelId{208};
    tessellation_labels.source_right_opposite = phoenix::LabelId{209};
    tessellation_labels.target_left_opposite = phoenix::LabelId{210};
    tessellation_labels.target_right_opposite = phoenix::LabelId{211};
    const auto tessellation =
        phoenix::partition::adapted::StraightCutTessellator{}.tessellate(
            arrangement.working->arrangement, bounded_face,
            production_cut_results.front(), trusted_cut, tessellation_labels);
    const auto tessellation_ok = tessellation.success()
        && tessellation.faces.size() == 2
        && tessellation.cut->data().label == phoenix::LabelId{202}
        && tessellation.cut->twin()->data().label == phoenix::LabelId{203}
        && tessellation.cut->face()->data().label == phoenix::LabelId{200}
        && tessellation.cut->twin()->face()->data().label == phoenix::LabelId{201};
    ok = ok && tessellation_ok;
    std::cout << "production straight-cut tessellation: "
        << tessellation_ok << '\n';
    auto tree_arrangement =
        phoenix::partition::ExactArrangementBuilder{}.build(projected.face);
    phoenix::partition::ExactArrangement::Face_handle tree_face;
    for (auto candidate = tree_arrangement.working->arrangement.faces_begin();
         candidate != tree_arrangement.working->arrangement.faces_end(); ++candidate)
        if (!candidate->is_unbounded()) tree_face = candidate;
    const auto tree_tessellation =
        phoenix::partition::adapted::StraightCutTessellator{}.tessellate_tree(
            tree_arrangement.working->arrangement, tree_face,
            production_cut_results.front(), cut_branching, 0,
            {{0, tessellation_labels}});
    const auto tree_tessellation_ok = tree_tessellation.success()
        && tree_tessellation.faces.size() == 2;
    ok = ok && tree_tessellation_ok;
    std::cout << "production recursive tessellation traversal: "
        << tree_tessellation_ok << '\n';
    auto collapsed_arrangement =
        phoenix::partition::ExactArrangementBuilder{}.build(projected.face);
    phoenix::partition::ExactArrangement::Face_handle collapsed_face;
    for (auto candidate = collapsed_arrangement.working->arrangement.faces_begin();
         candidate != collapsed_arrangement.working->arrangement.faces_end();
         ++candidate)
        if (!candidate->is_unbounded()) collapsed_face = candidate;
    std::vector<phoenix::partition::ExactKernel::Segment_2> collapsed_boundary;
    auto collapsed_edge = collapsed_face->outer_ccb();
    const auto collapsed_edge_end = collapsed_edge;
    do {
        collapsed_boundary.emplace_back(
            collapsed_edge->source()->point(), collapsed_edge->target()->point());
        ++collapsed_edge;
    } while (collapsed_edge != collapsed_edge_end);
    const auto collapsed_cut_segment = collapsed_boundary[0];
    const auto collapsed_source_piece = collapsed_boundary[3];
    const auto collapsed_target_piece = collapsed_boundary[1];
    phoenix::partition::CompatibilityRandomStream collapsed_random{654};
    PartitionView collapsed_view{repository, collapsed_random};
    collapsed_view.add_segment(SegmentInfo{CutSegmentId{0},
        collapsed_source_piece.source(), collapsed_source_piece.target()});
    collapsed_view.add_segment(SegmentInfo{CutSegmentId{2},
        collapsed_target_piece.source(), collapsed_target_piece.target()});
    collapsed_view.add_segment(SegmentInfo{CutSegmentId{4},
        collapsed_cut_segment.source(), collapsed_cut_segment.target()});
    collapsed_view.add_segment(SegmentInfo{CutSegmentId{5},
        collapsed_cut_segment.source(), collapsed_cut_segment.source()});
    collapsed_view.add_segment(SegmentInfo{CutSegmentId{6},
        collapsed_source_piece.source(), collapsed_source_piece.target()});
    collapsed_view.add_segment(SegmentInfo{CutSegmentId{7},
        collapsed_cut_segment.target(), collapsed_cut_segment.target()});
    collapsed_view.add_segment(SegmentInfo{CutSegmentId{8},
        collapsed_target_piece.source(), collapsed_target_piece.target()});
    const auto collapsed_tessellation =
        phoenix::partition::adapted::StraightCutTessellator{}.tessellate(
            collapsed_arrangement.working->arrangement, collapsed_face,
            collapsed_view, trusted_cut, tessellation_labels);
    const auto collapsed_tessellation_ok = collapsed_tessellation.success()
        && collapsed_tessellation.faces.size() == 1
        && collapsed_tessellation.left_face
            == phoenix::partition::ExactArrangement::Face_handle{}
        && collapsed_tessellation.right_face
            != phoenix::partition::ExactArrangement::Face_handle{}
        && collapsed_tessellation.faces.front()->data().label
            == phoenix::LabelId{201};
    ok = ok && collapsed_tessellation_ok;
    std::cout << "production collapsed-side tessellation: "
        << collapsed_tessellation_ok << '\n';
    auto repeat_arrangement =
        phoenix::partition::ExactArrangementBuilder{}.build(split_rectangle());
    phoenix::partition::ExactArrangement::Face_handle repeat_face;
    for (auto candidate = repeat_arrangement.working->arrangement.faces_begin();
         candidate != repeat_arrangement.working->arrangement.faces_end();
         ++candidate)
        if (!candidate->is_unbounded()) repeat_face = candidate;
    const auto repeat_distribution =
        phoenix::partition::adapted::distribute_repeat_by_count(
            {2, 1.0, 0.0, 0.0}, 3.0, 3.0);
    phoenix::partition::adapted::RepeatTessellationLabels repeat_labels;
    repeat_labels.primary.face = phoenix::LabelId{300};
    repeat_labels.primary.lower_edge = phoenix::LabelId{304};
    repeat_labels.primary.upper_edge = phoenix::LabelId{301};
    repeat_labels.primary.source_side = phoenix::LabelId{305};
    repeat_labels.primary.source_side_opposite = phoenix::LabelId{306};
    repeat_labels.primary.target_side = phoenix::LabelId{307};
    repeat_labels.primary.target_side_opposite = phoenix::LabelId{308};
    repeat_labels.secondary.face = phoenix::LabelId{302};
    repeat_labels.secondary.lower_edge = phoenix::LabelId{309};
    repeat_labels.secondary.upper_edge = phoenix::LabelId{303};
    repeat_labels.secondary.source_side = phoenix::LabelId{310};
    repeat_labels.secondary.source_side_opposite = phoenix::LabelId{311};
    repeat_labels.secondary.target_side = phoenix::LabelId{312};
    repeat_labels.secondary.target_side_opposite = phoenix::LabelId{313};
    const auto repeated =
        phoenix::partition::adapted::StraightCutTessellator{}
            .tessellate_repeat_interpolated(
                repeat_arrangement.working->arrangement, repeat_face,
                {{0, 0}, {0, 3}}, {{4, 0}, {4, 3}},
                repeat_distribution, repeat_labels);
    std::size_t primary_faces = 0;
    std::size_t secondary_faces = 0;
    for (const auto repeated_face : repeated.faces) {
        if (repeated_face->data().label == phoenix::LabelId{300})
            ++primary_faces;
        if (repeated_face->data().label == phoenix::LabelId{302})
            ++secondary_faces;
    }
    bool saw_directed_source_label = false;
    for (auto edge = repeat_arrangement.working->arrangement.halfedges_begin();
         edge != repeat_arrangement.working->arrangement.halfedges_end(); ++edge)
        if (edge->data().label == phoenix::LabelId{305}
            && edge->twin()->data().label == phoenix::LabelId{306})
            saw_directed_source_label = true;
    const auto repeat_tessellation_ok = repeated.success()
        && repeated.faces.size() == 3
        && primary_faces == 2 && secondary_faces == 1
        && saw_directed_source_label;
    ok = ok && repeat_tessellation_ok;
    std::cout << "production interpolated repeat tessellation: "
        << repeat_tessellation_ok << '\n';
    std::cout << "exact CGAL arrangement DCEL metadata: " << ok << '\n';
    std::cout << "production arrangement segment repository: " << ok << '\n';
    std::cout << "production trusted solver foundation: " << ok << '\n';
    std::cout << "production trusted branching: " << ok << '\n';
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
