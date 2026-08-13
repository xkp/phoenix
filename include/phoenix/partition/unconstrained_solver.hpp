#pragma once

// QUARANTINED COMPILE SPIKE: do not use as the Phoenix runtime partition solver.
// Port partition_model branch/angle code from production against ExactArrangement.

#include "phoenix/partition/solver_view.hpp"

#include <optional>

namespace phoenix::partition {

class AngleRangeSet;
class CompatibilityRandomStream;

struct CutPointSample {
    double source_fraction = 0.0;
    double target_fraction = 0.0;
};

struct CutSolution {
    ExactPoint2 source_point;
    ExactPoint2 target_point;
};

struct FixedAngleRestriction {
    double radians = 0.0;
};

[[nodiscard]] FixedAngleRestriction fixed_angle_from_reference(
    const SelectedSegment& reference, double offset_degrees);

struct CutSolveResult {
    std::vector<CutSolution> solutions;
    std::string error;
    [[nodiscard]] bool success() const noexcept { return !solutions.empty(); }
};

[[nodiscard]] CutSolveResult solve_unconstrained_cut(
    const SegmentRepository& repository, const CutSeed& seed,
    const std::vector<CutPointSample>& samples,
    std::optional<FixedAngleRestriction> fixed_angle = std::nullopt);

[[nodiscard]] CutSolveResult solve_angle_restricted_cut(
    const SegmentRepository& repository, const CutSeed& seed,
    CompatibilityRandomStream& random, const AngleRangeSet& angles,
    std::size_t variation_count = 4);

[[nodiscard]] SolverView apply_cut_solution(const SolverView& source_view,
    const CutDefinition& cut, const CutSolution& solution);

} // namespace phoenix::partition
