#pragma once

// QUARANTINED AND SUPERSEDED by ported/partition_solver_filters.h.

#include "phoenix/partition/arrangement_segment_repository.hpp"

namespace phoenix::partition::adapted {

struct BaseLengthPercentFilter {
    double minimum_percent = 0.0;
    double maximum_percent = 0.0;

    [[nodiscard]] bool operator()(const ArrangementRepoEdge& candidate,
        const ArrangementRepoEdge& reference) const;
};

struct BaseAngleFilter {
    double minimum_degrees = 0.0;
    double maximum_degrees = 0.0;

    [[nodiscard]] bool operator()(const ArrangementRepoEdge& first,
        const ArrangementRepoEdge& second) const;
};

} // namespace phoenix::partition::adapted
