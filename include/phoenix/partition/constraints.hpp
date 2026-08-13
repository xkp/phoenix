#pragma once

// QUARANTINED COMPILE SPIKE: constraint behavior oracle only. Production
// constraint workers must be mechanically ported before runtime integration.

#include "phoenix/partition/solver_view.hpp"

#include <string>

namespace phoenix::partition {

struct SegmentLengthRestriction {
    SegmentRef source_segment;
    SegmentRef target_segment;
    bool constrain_source = true;
    bool left_piece = true;
    double minimum_length = 0.0;
    double maximum_length = 0.0;
};

[[nodiscard]] bool apply_segment_length_restriction(SolverView& view,
    const SegmentLengthRestriction& restriction, std::string& error);

} // namespace phoenix::partition
