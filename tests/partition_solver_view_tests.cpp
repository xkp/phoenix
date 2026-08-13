#include "phoenix/partition/solver_view.hpp"
#include "phoenix/partition/constraints.hpp"
#include "phoenix/partition/angle_ranges.hpp"
#include "phoenix/partition/sampling.hpp"
#include "phoenix/partition/unconstrained_solver.hpp"

#include <CGAL/number_utils.h>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

phoenix::CanonicalGeometryRef rectangle()
{
    using namespace phoenix;
    return CanonicalGeometry::create(
        {{{0, 0, 0}, VertexId{1}, 0}, {{2, 0, 0}, VertexId{2}, 1},
            {{4, 0, 0}, VertexId{3}, 2}, {{4, 3, 0}, VertexId{4}, 3},
            {{0, 3, 0}, VertexId{5}, 4}},
        {{0, 0, 1, 4, invalid_geometry_index, invalid_geometry_index,
             HalfedgeId{10}, EdgeId{20}, LabelId{30}},
            {1, 0, 2, 0, invalid_geometry_index, invalid_geometry_index,
                HalfedgeId{11}, EdgeId{21}, LabelId{30}},
            {2, 0, 3, 1, invalid_geometry_index, invalid_geometry_index,
                HalfedgeId{12}, EdgeId{22}, LabelId{31}},
            {3, 0, 4, 2, invalid_geometry_index, invalid_geometry_index,
                HalfedgeId{13}, EdgeId{23}, LabelId{32}},
            {4, 0, 0, 3, invalid_geometry_index, invalid_geometry_index,
                HalfedgeId{14}, EdgeId{24}, LabelId{33}}},
        {{0, FaceId{40}, LabelId{41}}});
}

phoenix::partition::ExactWorkingFace concave_face()
{
    using namespace phoenix;
    using namespace phoenix::partition;
    ExactWorkingFace face;
    const std::vector<ExactPoint2> points{{0, 0}, {4, 0}, {4, 4}, {3, 4},
        {3, 1}, {1, 1}, {1, 4}, {0, 4}};
    for (std::size_t index = 0; index < points.size(); ++index) {
        face.boundary.push_back({points[index], VertexId{100 + index},
            HalfedgeId{200 + index}, EdgeId{300 + index},
            LabelId{static_cast<std::int32_t>(40 + index)},
            unassigned_label_id, unassigned_label_id});
    }
    return face;
}

} // namespace

int main()
{
    using namespace phoenix;
    using namespace phoenix::partition;

    const auto geometry = rectangle();
    const auto projection = ExactFaceProjector{}.project(*geometry, 0);
    std::string error;
    const std::vector<BaseSegmentDefinition> base_segments{
        {SegmentRef{0},
            {{LabelMatchField::current, LabelMatchComparison::equal, LabelId{30}}},
            4.0, 4.0},
        {SegmentRef{1}, {{LabelMatchField::current, LabelMatchComparison::equal, LabelId{31}}}},
        {SegmentRef{2}, {{LabelMatchField::current, LabelMatchComparison::equal, LabelId{32}}}},
        {SegmentRef{3}, {{LabelMatchField::current, LabelMatchComparison::equal, LabelId{33}}}},
    };
    CutLabels labels;
    const CutDefinition cut{0, SegmentRef{4}, SegmentRef{0}, SegmentRef{2}, labels};
    const PartitionPlan plan{4, {cut}, base_segments};
    const auto repository = SegmentRepository::from_boundary(projection.face, plan, error);
    SolverView view{*repository};
    const auto seed = select_cut_seed(view, cut, error);

    bool ok = projection.success() && repository.has_value() && seed.has_value()
        && seed->source->candidate->source_edge_id == EdgeId{20}
        && seed->source->candidate->boundary_indices.size() == 2
        && seed->target->candidate->source_edge_id == EdgeId{23}
        && seed->source->candidate->current_label == LabelId{30}
        && seed->target->candidate->current_label == LabelId{32};

    auto* mutable_source = view.selected_mutable(SegmentRef{0});
    const auto original_source = mutable_source->source;
    mutable_source->source = mutable_source->target;
    ok = ok && mutable_source->collapsed();
    view.reset();
    ok = ok && mutable_source->source == original_source && !mutable_source->collapsed();

    const auto solved = solve_unconstrained_cut(
        *repository, *seed, {{0.25, 0.75}, {0.5, 0.5}});
    ok = ok && solved.success() && solved.solutions.size() == 2;
    const auto cut_view = apply_cut_solution(view, cut, solved.solutions.back());
    ok = ok && cut_view.selected(SegmentRef{4}) != nullptr
        && cut_view.selected(SegmentRef{5}) != nullptr
        && cut_view.selected(SegmentRef{6}) != nullptr
        && cut_view.selected(SegmentRef{7}) != nullptr
        && cut_view.selected(SegmentRef{8}) != nullptr
        && cut_view.selected(SegmentRef{4})->source == solved.solutions.back().source_point
        && cut_view.selected(SegmentRef{4})->target == solved.solutions.back().target_point;

    const auto samples_a = generate_compatibility_samples(12345);
    const auto samples_b = generate_compatibility_samples(12345);
    const auto samples_c = generate_compatibility_samples(12346);
    ok = ok && samples_a.size() == production_cut_variation_count
        && samples_a.front().source_fraction == samples_b.front().source_fraction
        && samples_a.front().target_fraction == samples_b.front().target_fraction
        && samples_a.front().source_fraction != samples_c.front().source_fraction;
    SeedDerivationInput first_item_seed;
    first_item_seed.global_seed = 77;
    first_item_seed.node_id = 9;
    first_item_seed.call_path = {"partition"};
    first_item_seed.item_key = 100;
    auto second_item_seed = first_item_seed;
    second_item_seed.item_key = 101;
    const auto first_item_samples = generate_compatibility_samples(first_item_seed);
    const auto repeated_item_samples = generate_compatibility_samples(first_item_seed);
    const auto second_item_samples = generate_compatibility_samples(second_item_seed);
    ok = ok
        && first_item_samples.front().source_fraction
            == repeated_item_samples.front().source_fraction
        && first_item_samples.front().source_fraction
            != second_item_samples.front().source_fraction;
    CompatibilityRandomStream baseline_stream{500};
    static_cast<void>(baseline_stream.next());
    const auto baseline_second = baseline_stream.next();
    CompatibilityRandomStream shuffled_stream{500};
    std::vector<int> shuffle_values{1, 2, 3, 4};
    shuffled_stream.shuffle(shuffle_values);
    ok = ok && shuffled_stream.next() == baseline_second;

    const auto fixed_angle = solve_unconstrained_cut(
        *repository, *seed, {{0.25, 0.9}},
        fixed_angle_from_reference(*seed->source, 90.0));
    ok = ok && fixed_angle.success()
        && std::abs(CGAL::to_double(fixed_angle.solutions.front().source_point.x()
                - fixed_angle.solutions.front().target_point.x())) < 1e-8;

    AngleRangeSet angle_ranges;
    angle_ranges.restrict_relative(*seed->source, 90.0, 90.0);
    ok = ok && angle_ranges.restricted() && angle_ranges.ranges().size() == 1
        && angle_ranges.ranges().front().contains(std::acos(-1.0) / 2.0);
    angle_ranges.reset();
    angle_ranges.restrict_relative(*seed->source, 30.0, 60.0);
    ok = ok && angle_ranges.ranges().size() == 2;
    CompatibilityRandomStream cone_random_a{900};
    CompatibilityRandomStream cone_random_b{900};
    const auto cone_solutions_a = solve_angle_restricted_cut(
        *repository, *seed, cone_random_a, angle_ranges);
    const auto cone_solutions_b = solve_angle_restricted_cut(
        *repository, *seed, cone_random_b, angle_ranges);
    ok = ok && cone_solutions_a.success() && cone_solutions_b.success()
        && cone_solutions_a.solutions.size() == cone_solutions_b.solutions.size()
        && cone_solutions_a.solutions.front().source_point
            == cone_solutions_b.solutions.front().source_point
        && cone_solutions_a.solutions.front().target_point
            == cone_solutions_b.solutions.front().target_point;

    SolverView length_view{*repository};
    std::string length_constraint_error;
    const auto length_seed = select_cut_seed(length_view, cut, length_constraint_error);
    ok = ok && length_seed.has_value()
        && apply_segment_length_restriction(length_view,
            {SegmentRef{0}, SegmentRef{2}, true, true, 1.0, 2.0},
            length_constraint_error);
    const auto* restricted_source = length_view.selected(SegmentRef{0});
    ok = ok && restricted_source != nullptr
        && std::abs(CGAL::to_double((restricted_source->target
            - restricted_source->source).squared_length()) - 1.0) < 1e-8;

    const auto invalid_sample = solve_unconstrained_cut(
        *repository, *seed, {{-0.1, 0.5}});
    ok = ok && !invalid_sample.success() && !invalid_sample.error.empty();

    const auto concave = concave_face();
    const CutDefinition concave_cut{
        0, SegmentRef{2}, SegmentRef{0}, SegmentRef{1}, labels};
    const PartitionPlan concave_plan{2, {concave_cut},
        {{SegmentRef{0},
             {{LabelMatchField::current, LabelMatchComparison::equal, LabelId{40}}}},
            {SegmentRef{1},
                {{LabelMatchField::current, LabelMatchComparison::equal, LabelId{46}}}}}};
    std::string concave_error;
    const auto concave_repository =
        SegmentRepository::from_boundary(concave, concave_plan, concave_error);
    SolverView concave_view{*concave_repository};
    const auto concave_seed = select_cut_seed(concave_view, concave_cut, concave_error);
    const auto blocked = solve_unconstrained_cut(
        *concave_repository, *concave_seed, {{0.5, 0.5}});
    ok = ok && concave_repository->is_face_concave()
        && !blocked.success() && !blocked.error.empty();

    std::string duplicate_error;
    SolverView duplicate_view{*repository};
    const CutDefinition duplicate{1, SegmentRef{5}, SegmentRef{0}, SegmentRef{0}, labels};
    ok = ok && !select_cut_seed(duplicate_view, duplicate, duplicate_error).has_value()
        && !duplicate_error.empty();

    const PartitionPlan ambiguous_plan{1, {},
        {{SegmentRef{0},
            {{LabelMatchField::current, LabelMatchComparison::not_equal, LabelId{999}}}}}};
    const auto ambiguous_repository =
        SegmentRepository::from_boundary(projection.face, ambiguous_plan, error);
    SolverView ambiguous_view{*ambiguous_repository};
    std::string ambiguous_error;
    ok = ok && !ambiguous_view.select_unique(SegmentRef{0}, true, ambiguous_error)
        && !ambiguous_error.empty();

    const PartitionPlan duplicate_definition_plan{2, {},
        {{SegmentRef{0}, {}}, {SegmentRef{0}, {}}}};
    std::string definition_error;
    ok = ok && !SegmentRepository::from_boundary(
        projection.face, duplicate_definition_plan, definition_error).has_value()
        && !definition_error.empty();

    const PartitionPlan invalid_length_plan{1, {},
        {{SegmentRef{0}, {}, 5.0, 4.0}}};
    std::string length_error;
    ok = ok && !SegmentRepository::from_boundary(
        projection.face, invalid_length_plan, length_error).has_value()
        && !length_error.empty();

    std::cout << "exact repository and deterministic cut seed: " << ok << '\n';
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
