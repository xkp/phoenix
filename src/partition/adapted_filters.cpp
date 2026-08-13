#include "phoenix/partition/adapted_filters.hpp"

#include <CGAL/number_utils.h>

#include <cmath>

namespace phoenix::partition::adapted {

bool BaseLengthPercentFilter::operator()(const ArrangementRepoEdge& candidate,
    const ArrangementRepoEdge& reference) const
{
    auto minimum = minimum_percent * 0.01;
    auto maximum = maximum_percent * 0.01;
    minimum *= minimum;
    maximum *= maximum;
    const auto length = CGAL::to_double(candidate.segment.squared_length());
    const auto reference_length =
        CGAL::to_double(reference.segment.squared_length());
    return length >= minimum * reference_length
        && length <= maximum * reference_length;
}

bool BaseAngleFilter::operator()(const ArrangementRepoEdge& first,
    const ArrangementRepoEdge& second) const
{
    const auto first_vector = first.segment.to_vector();
    const auto second_vector = second.segment.to_vector();
    const auto cross = CGAL::to_double(first_vector.x() * second_vector.y()
        - first_vector.y() * second_vector.x());
    const auto dot = CGAL::to_double(first_vector * second_vector);
    auto angle_degrees = std::atan2(cross, dot) * 180.0 / std::acos(-1.0);
    if (angle_degrees < 0.0) angle_degrees += 180.0;
    if (angle_degrees >= 180.0) angle_degrees -= 180.0;
    if (angle_degrees > 90.0) angle_degrees = 180.0 - angle_degrees;
    return angle_degrees >= minimum_degrees - 1e-5
        && angle_degrees <= maximum_degrees + 1e-5;
}

} // namespace phoenix::partition::adapted
