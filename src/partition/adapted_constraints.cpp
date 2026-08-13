#include "phoenix/partition/adapted_constraints.hpp"

#include <CGAL/number_utils.h>
#include <CGAL/intersections.h>

#include <cmath>

namespace phoenix::partition::adapted {
namespace {

bool restrict_segment(trusted::SegmentInfo& segment, double minimum_length,
    double maximum_length, bool reversed, std::string& error)
{
    auto source = segment.source;
    auto target = segment.target;
    auto direction = segment.original_target - segment.original_source;
    const auto length = std::sqrt(CGAL::to_double(direction.squared_length()));
    if (length > 0.0) direction = direction / ExactKernel::FT{length};

    if (minimum_length > length || maximum_length < 0.0) {
        error = "segment too short";
        return false;
    }

    if (reversed) {
        if (minimum_length > 0.0 && minimum_length < length)
            target = segment.original_target
                - direction * ExactKernel::FT{minimum_length};
    } else if (maximum_length < length) {
        target = segment.original_source
            + direction * ExactKernel::FT{maximum_length};
    }

    if (reversed) {
        if (maximum_length < length)
            source = segment.original_target
                - direction * ExactKernel::FT{maximum_length};
    } else if (minimum_length > 0.0) {
        source = segment.original_source
            + direction * ExactKernel::FT{minimum_length};
    }

    const ExactKernel::Line_2 old_source{
        segment.source, direction.perpendicular(CGAL::CLOCKWISE)};
    if (!old_source.has_on_positive_side(source)) source = segment.source;
    const ExactKernel::Line_2 old_target{
        segment.target, direction.perpendicular(CGAL::COUNTERCLOCKWISE)};
    if (!old_target.has_on_positive_side(target)) target = segment.target;

    const auto source_distance = CGAL::squared_distance(
        segment.original_source, source);
    const auto target_distance = CGAL::squared_distance(
        segment.original_source, target);
    if (source_distance > target_distance) {
        error = "result segment empty";
        return false;
    }
    segment.source = source;
    segment.target = target;
    return true;
}

bool restrict_cut_segment(trusted::PartitionView& view,
    const trusted::TrustedCut& cut, bool is_source, bool is_left,
    double minimum_length, double maximum_length, std::string& error)
{
    auto* source = view.segment(cut.source);
    auto* target = view.segment(cut.target);
    if (source == nullptr || target == nullptr) {
        error = "cut source or target segment is unavailable";
        return false;
    }
    auto* segment = is_source ? source : target;
    const auto source_midpoint = CGAL::midpoint(source->source, source->target);
    const auto target_midpoint = CGAL::midpoint(target->source, target->target);
    const ExactKernel::Line_2 test_line{source_midpoint, target_midpoint};
    const bool source_is_right = test_line.has_on_negative_side(segment->source);
    return restrict_segment(*segment, minimum_length, maximum_length,
        !(is_left ^ source_is_right), error);
}

bool restrict_positive_halfplane(const ExactKernel::Line_2& line,
    trusted::SegmentInfo& segment)
{
    const bool source_negative = line.has_on_negative_side(segment.source);
    const bool target_negative = line.has_on_negative_side(segment.target);
    const auto intersection = CGAL::intersection(segment.segment(), line);
    if (!intersection) return !source_negative;
    const auto* point = std::get_if<ExactPoint2>(&*intersection);
    if (point == nullptr) return !source_negative;
    if (source_negative) segment.source = *point;
    if (target_negative) segment.target = *point;
    return true;
}

bool restrict_distance(trusted::PartitionView& view,
    const trusted::BranchingModel& model, trusted::CutSegmentId cut_segment,
    trusted::CutSegmentId reference, bool has_minimum, double minimum_distance,
    bool has_maximum, double maximum_distance, std::string& error)
{
    auto* reference_info = view.segment(reference);
    if (reference_info == nullptr) {
        error = "distance reference segment is unavailable";
        return false;
    }
    auto direction = reference_info->target - reference_info->source;
    direction = direction.perpendicular(CGAL::COUNTERCLOCKWISE);
    const auto direction_length = std::sqrt(
        CGAL::to_double(direction.squared_length()));
    if (direction_length > 0.0)
        direction = direction / ExactKernel::FT{direction_length};

    std::int32_t cut_index = -1;
    trusted::CutSegmentType cut_type;
    if (!model.is_cut_segment(cut_segment, cut_index, cut_type)
        || !cut_type.is_cut()) {
        error = "distance target is not a cut segment";
        return false;
    }
    const auto* cut = model.get_cut(cut_index);
    auto* cut_source = cut == nullptr ? nullptr : view.segment(cut->source);
    auto* cut_target = cut == nullptr ? nullptr : view.segment(cut->target);
    if (cut_source == nullptr || cut_target == nullptr) {
        error = "cut source or target segment is unavailable";
        return false;
    }

    auto reference_segment = reference_info->segment();
    if (model.is_cut(reference)) {
        const auto reference_line = reference_segment.supporting_line();
        if (!reference_line.has_on_positive_side(cut_source->source)
            && !reference_line.has_on_positive_side(cut_source->target)) {
            direction = -direction;
            reference_segment = reference_segment.opposite();
        }
    }

    if (has_minimum) {
        const auto first = reference_segment.source()
            + direction * ExactKernel::FT{minimum_distance};
        const auto second = reference_segment.target()
            + direction * ExactKernel::FT{minimum_distance};
        const ExactKernel::Line_2 minimum_line{first, second};
        if (!minimum_line.is_degenerate()
            && !(restrict_positive_halfplane(minimum_line, *cut_source)
                && restrict_positive_halfplane(minimum_line, *cut_target))) {
            view.notify_error();
            error = "minimum distance produced an empty cut range";
            return false;
        }
    }
    if (has_maximum) {
        const auto first = reference_segment.source()
            + direction * ExactKernel::FT{maximum_distance};
        const auto second = reference_segment.target()
            + direction * ExactKernel::FT{maximum_distance};
        const ExactKernel::Line_2 maximum_line{second, first};
        if (!maximum_line.is_degenerate()
            && !(restrict_positive_halfplane(maximum_line, *cut_source)
                && restrict_positive_halfplane(maximum_line, *cut_target))) {
            view.notify_error();
            error = "maximum distance produced an empty cut range";
            return false;
        }
    }
    return true;
}

} // namespace

bool RestrictCutSegmentLength::apply(trusted::PartitionView& view,
    const trusted::BranchingModel& model, std::string& error) const
{
    error.clear();
    const auto* cut = model.get_cut(cut_id);
    if (cut == nullptr) {
        error = "cut is unavailable";
        return false;
    }
    return restrict_cut_segment(view, *cut, is_source, is_left,
        minimum_length, maximum_length, error);
}

bool RestrictCutSegmentLengthPercent::apply(trusted::PartitionView& view,
    const trusted::BranchingModel& model, std::string& error) const
{
    error.clear();
    const auto* cut = model.get_cut(cut_id);
    if (cut == nullptr) {
        error = "cut is unavailable";
        return false;
    }
    trusted::SegmentInfo* reference_segment = nullptr;
    if (reference.valid()) {
        reference_segment = view.segment(reference);
        if (reference_segment == nullptr)
            reference_segment = model.branch_simple(view, reference, false);
        if (reference_segment == nullptr) return true;
    } else {
        reference_segment = view.segment(is_source ? cut->source : cut->target);
    }
    if (reference_segment == nullptr) {
        error = "length reference segment is unavailable";
        return false;
    }
    const auto reference_length = std::sqrt(CGAL::to_double(
        CGAL::squared_distance(reference_segment->original_source,
            reference_segment->original_target)));
    return restrict_cut_segment(view, *cut, is_source, is_left,
        minimum_percent * 0.01 * reference_length,
        maximum_percent * 0.01 * reference_length, error);
}

void RestrictCutAngle::apply(trusted::PartitionView& view,
    const trusted::BranchingModel& model,
    trusted::PartitionViewList& result) const
{
    trusted::PartitionViewList views;
    if (view.segment(reference) == nullptr) {
        if (model.is_base_segment(reference))
            model.branch(view, reference, views, false);
    } else {
        views.push_back(view);
    }
    const auto pi = std::acos(-1.0);
    const auto minimum_radians = minimum_degrees * pi / 180.0;
    const auto maximum_radians = maximum_degrees * pi / 180.0;
    for (auto& candidate : views) {
        const auto* segment = candidate.segment(reference);
        auto vector = segment->original_target - segment->original_source;
        auto reference_angle = std::atan2(
            CGAL::to_double(vector.y()), CGAL::to_double(vector.x()));
        if (reference_angle < 0.0) reference_angle += pi;
        candidate.restrict_angles(
            reference_angle, minimum_radians, maximum_radians);
        if (!candidate.angles.empty()) result.push_back(candidate);
    }
}

bool RestrictCutDistance::apply(trusted::PartitionView& view,
    const trusted::BranchingModel& model, std::string& error) const
{
    error.clear();
    return restrict_distance(view, model, cut_segment, reference,
        has_minimum, minimum_distance, has_maximum, maximum_distance, error);
}

bool RestrictCutDistancePercent::apply(trusted::PartitionView& view,
    const trusted::BranchingModel& model, std::string& error) const
{
    error.clear();
    if (!percentage_reference.valid()) {
        error = "percentage distance reference is invalid";
        return false;
    }
    const auto* segment = view.segment(percentage_reference);
    if (segment == nullptr) {
        error = "percentage distance reference is unavailable";
        return false;
    }
    const auto length = std::sqrt(CGAL::to_double(CGAL::squared_distance(
        segment->original_source, segment->original_target)));
    return restrict_distance(view, model, cut_segment, reference,
        has_minimum, minimum_percent * 0.01 * length,
        has_maximum, maximum_percent * 0.01 * length, error);
}

bool RestrictDistanceExtra::apply(trusted::PartitionView& view,
    const trusted::BranchingModel& model, std::string& error) const
{
    error.clear();
    const bool angle_collapsed = view.angles.size() == 1
        && view.angles.front().minimum_angle
            == view.angles.front().maximum_angle;
    if (!angle_collapsed) return true;

    const auto* cut = model.get_cut(cut_id);
    const auto* child_cut = model.get_cut(child_cut_id);
    if (cut == nullptr || child_cut == nullptr) {
        error = "parent or child cut is unavailable";
        return false;
    }
    auto* source = view.segment(cut->source);
    auto* target = view.segment(cut->target);
    if (source == nullptr || target == nullptr) {
        error = "parent cut source or target is unavailable";
        return false;
    }

    auto find_child_segment = [&view, &model](trusted::CutSegmentId id) {
        auto* segment = view.segment(id);
        if (segment == nullptr)
            segment = view.segment(model.get_parent_segment(id));
        if (segment == nullptr) segment = model.branch_simple(view, id, true);
        return segment;
    };
    auto* child_source = find_child_segment(child_cut->source);
    auto* child_target = find_child_segment(child_cut->target);
    if (child_source == nullptr || child_target == nullptr) return true;

    const auto angle = view.angles.front().minimum_angle;
    auto cut_direction = ExactKernel::Vector_2{std::cos(angle), std::sin(angle)};
    const ExactKernel::Line_2 source_line{
        source->original_source, source->original_target};
    if (source_line.oriented_side(target->original_source)
        != source_line.oriented_side(source->original_source + cut_direction))
        cut_direction = -cut_direction;
    const auto direction_to_child = cut_direction.perpendicular(
        is_left ? CGAL::COUNTERCLOCKWISE : CGAL::CLOCKWISE);

    const ExactPoint2 points[4]{child_source->original_source,
        child_source->original_target, child_target->original_source,
        child_target->original_target};
    double evaluation[4];
    for (std::size_t index = 0; index < 4; ++index)
        evaluation[index] = CGAL::to_double(direction_to_child
            * (points[index] - CGAL::ORIGIN));
    const auto source_index = evaluation[0] > evaluation[1] ? 0U : 1U;
    const auto target_index = evaluation[2] > evaluation[3] ? 2U : 3U;
    const auto reference_point = points[
        evaluation[source_index] < evaluation[target_index]
            ? source_index : target_index];
    const auto cut_point = reference_point
        - direction_to_child * ExactKernel::FT{minimum_distance};
    const ExactKernel::Line_2 cut_line{
        cut_point, is_left ? -cut_direction : cut_direction};
    if (source->restrict_line(cut_line) && target->restrict_line(cut_line))
        return true;
    error = "extra distance produced an empty parent cut range";
    return false;
}

} // namespace phoenix::partition::adapted
