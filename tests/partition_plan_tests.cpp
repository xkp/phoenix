#include "phoenix/partition/plan.hpp"
#include "phoenix/partition/repeat_distribution.hpp"

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <type_traits>

int main()
{
    using namespace phoenix;
    using namespace phoenix::partition;

    CutLabels labels;
    labels.face_left = LabelId{100};
    labels.face_right = LabelId{101};
    labels.cut_left = LabelId{102};
    labels.cut_right = LabelId{103};
    labels.source_left = LabelId{104};
    labels.source_right = LabelId{105};
    labels.target_left = LabelId{106};
    labels.target_right = LabelId{107};
    labels.source_left_opposite = LabelId{108};
    labels.source_right_opposite = LabelId{109};
    labels.target_left_opposite = LabelId{110};
    labels.target_right_opposite = LabelId{111};

    const std::vector<BaseSegmentDefinition> base_segments{
        {SegmentRef{0}, {{LabelMatchField::current, LabelMatchComparison::equal, LabelId{12}}}},
    };
    const PartitionPlan plan{1,
        {CutDefinition{0, SegmentRef{4}, SegmentRef{0}, SegmentRef{2}, labels}},
        base_segments,
        {{0, PlanPriority::apply_cut, 3, ApplyCutStep{0}},
            {0, PlanPriority::select_edges, 0,
                SelectEdgesStep{SegmentRef{0}, SegmentRef{2}}},
            {0, PlanPriority::length_constraint, 2, ConstraintStep{7}},
            {0, PlanPriority::distance_constraint, 1, ConstraintStep{8}}}};
    const auto& cut = plan.cuts().front();
    const auto ordered = plan.ordered_steps();
    const auto by_count = adapted::distribute_repeat_by_count(
        {2, 1.0, 1.0, 1.0}, 10.0, 12.0);
    const auto by_length = adapted::distribute_repeat_by_length(
        {3.0, 1.0, 1.0, 1.0, 0, adapted::RepeatAdjustMode::primary},
        10.0, 12.0);
    const bool ok = plan.base_segment_count() == 1 && plan.cuts().size() == 1
        && cut.id() == 0 && cut.result() == SegmentRef{4}
        && cut.source() == SegmentRef{0} && cut.target() == SegmentRef{2}
        && cut.labels().face_left == LabelId{100}
        && cut.labels().target_right_opposite == LabelId{111}
        && plan.base_segments().front().predicates.front().label == LabelId{12}
        && std::is_same_v<decltype(plan.cuts()), const std::vector<CutDefinition>&>
        && ordered.size() == 4
        && ordered[0].priority == PlanPriority::select_edges
        && ordered[1].priority == PlanPriority::distance_constraint
        && ordered[2].priority == PlanPriority::length_constraint
        && ordered[3].priority == PlanPriority::apply_cut
        && by_count.success() && by_count.count == 2
        && std::abs(by_count.slope.primary_left - 3.5) < 1e-9
        && std::abs(by_count.slope.primary_right - 4.5) < 1e-9
        && by_length.success() && by_length.count == 2
        && std::abs(by_length.slope.primary_left - 3.5) < 1e-9
        && std::abs(by_length.slope.primary_right - 4.5) < 1e-9;

    std::cout << "immutable partition plan and directed labels: " << ok << '\n';
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
