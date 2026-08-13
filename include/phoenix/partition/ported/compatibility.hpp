#pragma once

#include "phoenix/partition/arrangement_segment_repository.hpp"
#include "phoenix/partition/compat/geometry_types.hpp"

#include <cassert>
#include <cmath>

namespace phoenix::partition::ported {

// Phoenix boundary replacement for vm::variable_value. Linking resolves the
// value before the production kernel is entered.
struct linked_value {
    double resolved = 0.0;
    [[nodiscard]] double value() const noexcept { return resolved; }
};

using repo_edge2 = ArrangementRepoEdge;

inline double angle_between(const ExactKernel::Segment_2& first,
    const ExactKernel::Segment_2& second)
{
    const auto left = first.to_vector();
    const auto right = second.to_vector();
    return std::atan2(CGAL::to_double(left.x() * right.y()
            - left.y() * right.x()),
        CGAL::to_double(left * right));
}

} // namespace phoenix::partition::ported
