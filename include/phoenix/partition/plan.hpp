#pragma once

#include "phoenix/labels.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace phoenix::partition {

class SegmentRef {
public:
    explicit constexpr SegmentRef(std::int32_t value) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::int32_t value() const noexcept { return value_; }

    friend constexpr bool operator==(SegmentRef left, SegmentRef right) noexcept
    {
        return left.value_ == right.value_;
    }

    friend constexpr bool operator<(SegmentRef left, SegmentRef right) noexcept
    {
        return left.value_ < right.value_;
    }

private:
    std::int32_t value_;
};

struct CutLabels {
    LabelId face_left = unassigned_label_id;
    LabelId face_right = unassigned_label_id;
    LabelId cut_left = unassigned_label_id;
    LabelId cut_right = unassigned_label_id;
    LabelId source_left = unassigned_label_id;
    LabelId source_right = unassigned_label_id;
    LabelId target_left = unassigned_label_id;
    LabelId target_right = unassigned_label_id;
    LabelId source_left_opposite = unassigned_label_id;
    LabelId source_right_opposite = unassigned_label_id;
    LabelId target_left_opposite = unassigned_label_id;
    LabelId target_right_opposite = unassigned_label_id;
};

enum class LabelMatchField {
    current,
    opposite,
    opposite_face,
};

enum class LabelMatchComparison {
    equal,
    not_equal,
};

struct LabelMatchPredicate {
    LabelMatchField field = LabelMatchField::current;
    LabelMatchComparison comparison = LabelMatchComparison::equal;
    LabelId label = unassigned_label_id;
};

struct BaseSegmentDefinition {
    SegmentRef id;
    std::vector<LabelMatchPredicate> predicates;
    std::optional<double> minimum_length;
    std::optional<double> maximum_length;
};

enum class PlanPriority : std::int32_t {
    select_edges = 1,
    select_edges_secondary = 2,
    distance_constraint = 3,
    length_constraint = 4,
    angle_constraint = 5,
    precut_constraint = 8,
    apply_cut = 9,
};

struct SelectEdgesStep {
    SegmentRef source;
    std::optional<SegmentRef> target;
    bool check_orientation = true;
    bool randomize_source = false;
    bool randomize_target = false;
};

struct ApplyCutStep {
    std::int32_t cut_id = -1;
};

struct ConstraintStep {
    std::int32_t constraint_index = -1;
};

using PlanStepPayload = std::variant<
    SelectEdgesStep, ConstraintStep, ApplyCutStep>;

struct ScheduledPlanStep {
    std::int32_t cut_id = -1;
    PlanPriority priority = PlanPriority::angle_constraint;
    std::int32_t insertion_order = -1;
    PlanStepPayload payload;
};

class CutDefinition {
public:
    CutDefinition(std::int32_t id, SegmentRef result, SegmentRef source,
        SegmentRef target, CutLabels labels) noexcept
        : id_(id), result_(result), source_(source), target_(target),
          labels_(labels)
    {
    }

    [[nodiscard]] std::int32_t id() const noexcept { return id_; }
    [[nodiscard]] SegmentRef result() const noexcept { return result_; }
    [[nodiscard]] SegmentRef source() const noexcept { return source_; }
    [[nodiscard]] SegmentRef target() const noexcept { return target_; }
    [[nodiscard]] const CutLabels& labels() const noexcept { return labels_; }

private:
    std::int32_t id_;
    SegmentRef result_;
    SegmentRef source_;
    SegmentRef target_;
    CutLabels labels_;
};

class PartitionPlan {
public:
    PartitionPlan(std::int32_t base_segment_count, std::vector<CutDefinition> cuts,
        std::vector<BaseSegmentDefinition> base_segments = {},
        std::vector<ScheduledPlanStep> steps = {})
        : base_segment_count_(base_segment_count), cuts_(std::move(cuts)),
          base_segments_(std::move(base_segments)), steps_(std::move(steps))
    {
    }

    [[nodiscard]] std::vector<ScheduledPlanStep> ordered_steps() const
    {
        auto result = steps_;
        std::stable_sort(result.begin(), result.end(), [](const auto& left,
            const auto& right) {
            if (left.cut_id != right.cut_id) return left.cut_id < right.cut_id;
            if (left.priority != right.priority)
                return left.priority < right.priority;
            return left.insertion_order < right.insertion_order;
        });
        return result;
    }

    [[nodiscard]] const std::vector<BaseSegmentDefinition>& base_segments() const noexcept
    {
        return base_segments_;
    }

    [[nodiscard]] std::int32_t base_segment_count() const noexcept
    {
        return base_segment_count_;
    }

    [[nodiscard]] const std::vector<CutDefinition>& cuts() const noexcept
    {
        return cuts_;
    }

private:
    std::int32_t base_segment_count_;
    std::vector<CutDefinition> cuts_;
    std::vector<BaseSegmentDefinition> base_segments_;
    std::vector<ScheduledPlanStep> steps_;
};

} // namespace phoenix::partition
