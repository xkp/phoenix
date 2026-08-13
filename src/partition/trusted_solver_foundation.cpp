#include "phoenix/partition/trusted_solver_foundation.hpp"

#include <CGAL/intersections.h>

#include <cmath>
#include <limits>
#include <variant>

namespace phoenix::partition::trusted {

SegmentInfo::SegmentInfo(CutSegmentId id_value, const ArrangementRepoEdge& edge)
    : id(id_value), source(edge.segment.source()), target(edge.segment.target()),
      original_source(edge.segment.source()), original_target(edge.segment.target()),
      repository_edge(edge)
{
}

SegmentInfo::SegmentInfo(CutSegmentId id_value,
    const ExactPoint2& source_value, const ExactPoint2& target_value)
    : id(id_value), source(source_value), target(target_value),
      original_source(source_value), original_target(target_value)
{
}

void SegmentInfo::reset()
{
    source = original_source;
    target = original_target;
}

ExactKernel::Segment_2 SegmentInfo::segment() const
{
    return {original_source, original_target};
}

bool SegmentInfo::collapsed() const
{
    return source == target;
}

bool SegmentInfo::restrict_line(const ExactKernel::Line_2& line)
{
    const ExactKernel::Segment_2 current{source, target};
    const auto source_negative = line.has_on_negative_side(source);
    const auto target_negative = line.has_on_negative_side(target);
    const auto intersection = CGAL::intersection(current, line);
    const auto* point = intersection
        ? std::get_if<ExactPoint2>(&*intersection) : nullptr;
    if (point == nullptr) return !source_negative;
    if (source_negative) source = *point;
    if (target_negative) target = *point;
    return true;
}

AngleRange::AngleRange()
    : minimum_angle(static_cast<double>(std::numeric_limits<std::int32_t>::min())),
      maximum_angle(static_cast<double>(std::numeric_limits<std::int32_t>::max()))
{
}

AngleRange::AngleRange(double minimum_value, double maximum_value)
    : minimum_angle(wrap(minimum_value)), maximum_angle(wrap(maximum_value))
{
}

double AngleRange::wrap(double angle)
{
    const auto pi = std::acos(-1.0);
    angle = std::fmod(angle, 2.0 * pi);
    return angle > pi ? angle - 2.0 * pi
        : (angle <= -pi ? angle + 2.0 * pi : angle);
}

void AngleRange::reset()
{
    minimum_angle = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    maximum_angle = static_cast<double>(std::numeric_limits<std::int32_t>::max());
}

bool AngleRange::is_restricted() const
{
    return minimum_angle > static_cast<double>(std::numeric_limits<std::int32_t>::min());
}

bool AngleRange::inside(double angle) const
{
    if (minimum_angle <= maximum_angle)
        return minimum_angle - 1e-5 <= angle && angle <= maximum_angle + 1e-5;
    return minimum_angle - 1e-5 <= angle || angle <= maximum_angle + 1e-5;
}

AngleRange AngleRange::opposite() const
{
    const auto pi = std::acos(-1.0);
    return {minimum_angle + pi, maximum_angle + pi};
}

bool AngleRange::is_opposite(const AngleRange& other) const
{
    const auto candidate = other.opposite();
    return std::abs(minimum_angle - candidate.minimum_angle) <= 1e-5
        && std::abs(maximum_angle - candidate.maximum_angle) <= 1e-5;
}

bool AngleRange::intersect(const AngleRange& other, AngleRange& result) const
{
    result.reset();
    if (inside(other.minimum_angle)) result.minimum_angle = other.minimum_angle;
    else if (other.inside(minimum_angle)) result.minimum_angle = minimum_angle;
    else return false;
    result.maximum_angle = inside(other.maximum_angle)
        ? other.maximum_angle : maximum_angle;
    return true;
}

PartitionView::PartitionView(const ArrangementSegmentRepository& repository_value,
    CompatibilityRandomStream& random_value)
    : repository(&repository_value), random(&random_value), angles(1)
{
}

PartitionView::PartitionView(const PartitionView& other)
    : repository(other.repository), random(other.random), cut_index(other.cut_index),
      instruction_index(other.instruction_index), segments(other.segments),
      error(other.error), quality(other.quality), angles(other.angles),
      has_errors(false), cut_middle_line(other.cut_middle_line)
{
}

void PartitionView::copy(const PartitionView& other)
{
    cut_index = other.cut_index;
    instruction_index = other.instruction_index;
    error = other.error;
    segments = other.segments;
    angles = other.angles;
    quality = 0.0;
    cut_middle_line.reset();
}

const SegmentInfo* PartitionView::segment(CutSegmentId id) const
{
    const auto found = segments.find(id);
    return found == segments.end() ? nullptr : &found->second;
}

SegmentInfo* PartitionView::segment(CutSegmentId id)
{
    const auto found = segments.find(id);
    return found == segments.end() ? nullptr : &found->second;
}

bool PartitionView::has_segment(CutSegmentId id) const
{
    return segment(id) != nullptr;
}

bool PartitionView::has_edge(const ArrangementRepoEdge& edge) const
{
    for (const auto& item : segments)
        if (item.second.repository_edge == edge) return true;
    return false;
}

SegmentInfo* PartitionView::add_segment(const SegmentInfo& info)
{
    segments.erase(info.id);
    const auto inserted = segments.emplace(info.id, info);
    return &inserted.first->second;
}

bool PartitionView::is_angle_restricted() const
{
    return angles.size() != 1 || angles.front().is_restricted();
}

void PartitionView::reset()
{
    reset_angle_restriction();
    for (auto& item : segments) item.second.reset();
}

void PartitionView::reset_angle_restriction()
{
    angles.assign(1, AngleRange{});
}

void PartitionView::restrict_angles(double reference_angle,
    double minimum, double maximum)
{
    AngleRange first{reference_angle + minimum, reference_angle + maximum};
    AngleRange second{reference_angle - maximum, reference_angle - minimum};
    bool use_second = true;
    if (first.inside(second.maximum_angle)) {
        first.minimum_angle = second.minimum_angle;
        use_second = false;
    } else if (first.is_opposite(second)) {
        use_second = false;
    }

    const auto first_opposite = first.opposite();
    const auto second_opposite = second.opposite();
    std::vector<AngleRange> new_angles;
    for (const auto& existing : angles) {
        AngleRange result;
        if (existing.intersect(first, result)
            || existing.intersect(first_opposite, result))
            new_angles.push_back(result);
        if (use_second && (existing.intersect(second, result)
                || existing.intersect(second_opposite, result)))
            new_angles.push_back(result);
    }
    angles.swap(new_angles);
}

void PartitionView::notify_error(CutSegmentId error_id)
{
    if (has_errors) return;
    has_errors = true;
    first_error = error_id;
}

} // namespace phoenix::partition::trusted
