#include "phoenix/partition/angle_ranges.hpp"

#include <CGAL/number_utils.h>

#include <cmath>

namespace phoenix::partition {
namespace {

constexpr double angle_tolerance = 1e-5;

double wrap(double angle)
{
    const auto pi = std::acos(-1.0);
    angle = std::fmod(angle, 2.0 * pi);
    return angle > pi ? angle - 2.0 * pi
        : (angle <= -pi ? angle + 2.0 * pi : angle);
}

} // namespace

AngleRange::AngleRange(double minimum_radians, double maximum_radians)
    : minimum(wrap(minimum_radians)), maximum(wrap(maximum_radians)), restricted(true)
{
}

bool AngleRange::contains(double angle) const noexcept
{
    if (!restricted) return true;
    if (minimum <= maximum)
        return minimum - angle_tolerance <= angle && angle <= maximum + angle_tolerance;
    return minimum - angle_tolerance <= angle || angle <= maximum + angle_tolerance;
}

AngleRange AngleRange::opposite() const
{
    if (!restricted) return {};
    const auto pi = std::acos(-1.0);
    return {minimum + pi, maximum + pi};
}

bool AngleRange::is_opposite(const AngleRange& other) const
{
    if (!restricted || !other.restricted) return false;
    const auto opposite_range = other.opposite();
    return std::abs(minimum - opposite_range.minimum) <= angle_tolerance
        && std::abs(maximum - opposite_range.maximum) <= angle_tolerance;
}

bool AngleRange::intersect(const AngleRange& other, AngleRange& result) const
{
    if (!restricted) { result = other; return true; }
    if (!other.restricted) { result = *this; return true; }
    if (contains(other.minimum)) result.minimum = other.minimum;
    else if (other.contains(minimum)) result.minimum = minimum;
    else return false;
    result.maximum = contains(other.maximum) ? other.maximum : maximum;
    result.restricted = true;
    return true;
}

bool AngleRangeSet::restricted() const noexcept
{
    return ranges_.size() != 1 || ranges_.front().restricted;
}

void AngleRangeSet::restrict_relative(const SelectedSegment& reference,
    double minimum_degrees, double maximum_degrees)
{
    const auto vector = reference.original_target - reference.original_source;
    auto reference_angle = std::atan2(
        CGAL::to_double(vector.y()), CGAL::to_double(vector.x()));
    const auto pi = std::acos(-1.0);
    if (reference_angle < 0.0) reference_angle += pi;
    const auto minimum = minimum_degrees * pi / 180.0;
    const auto maximum = maximum_degrees * pi / 180.0;
    AngleRange first{reference_angle + minimum, reference_angle + maximum};
    AngleRange second{reference_angle - maximum, reference_angle - minimum};
    bool use_second = true;
    if (first.contains(second.maximum)) {
        first.minimum = second.minimum;
        use_second = false;
    } else if (first.is_opposite(second)) {
        use_second = false;
    }

    std::vector<AngleRange> restricted_ranges;
    for (const auto& existing : ranges_) {
        AngleRange result;
        if (existing.intersect(first, result)
            || existing.intersect(first.opposite(), result))
            restricted_ranges.push_back(result);
        if (use_second && (existing.intersect(second, result)
                || existing.intersect(second.opposite(), result)))
            restricted_ranges.push_back(result);
    }
    ranges_ = std::move(restricted_ranges);
}

} // namespace phoenix::partition
