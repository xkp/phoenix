#pragma once

// QUARANTINED COMPILE SPIKE: retained temporarily as a behavioral oracle for
// the direct port of production angle_range and partition_view.

#include "phoenix/partition/solver_view.hpp"

#include <vector>

namespace phoenix::partition {

struct AngleRange {
    double minimum = 0.0;
    double maximum = 0.0;
    bool restricted = false;

    AngleRange() = default;
    AngleRange(double minimum_radians, double maximum_radians);
    [[nodiscard]] bool contains(double angle) const noexcept;
    [[nodiscard]] AngleRange opposite() const;
    [[nodiscard]] bool is_opposite(const AngleRange& other) const;
    [[nodiscard]] bool intersect(const AngleRange& other, AngleRange& result) const;
};

class AngleRangeSet {
public:
    AngleRangeSet() : ranges_(1) {}
    void reset() { ranges_.assign(1, AngleRange{}); }
    [[nodiscard]] bool restricted() const noexcept;
    [[nodiscard]] const std::vector<AngleRange>& ranges() const noexcept { return ranges_; }
    void restrict_relative(const SelectedSegment& reference,
        double minimum_degrees, double maximum_degrees);

private:
    std::vector<AngleRange> ranges_;
};

} // namespace phoenix::partition
