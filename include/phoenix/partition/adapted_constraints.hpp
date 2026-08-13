#pragma once

// QUARANTINED BEHAVIORAL SCAFFOLDING. Do not extend or link into the partition
// runtime. Replace with mechanically adapted production sources under ported/.

#include "phoenix/partition/trusted_branching.hpp"

#include <string>

namespace phoenix::partition::adapted {

// Production restrict_cut_segment_length[_pct] geometry worker. Inputs have
// already been resolved from the legacy VM by the Phoenix linking boundary.
struct RestrictCutSegmentLength {
    std::int32_t cut_id = -1;
    bool is_source = false;
    bool is_left = false;
    double minimum_length = 0.0;
    double maximum_length = 0.0;

    [[nodiscard]] bool apply(trusted::PartitionView& view,
        const trusted::BranchingModel& model, std::string& error) const;
};

struct RestrictCutSegmentLengthPercent {
    std::int32_t cut_id = -1;
    bool is_source = false;
    bool is_left = false;
    double minimum_percent = 0.0;
    double maximum_percent = 0.0;
    trusted::CutSegmentId reference;

    [[nodiscard]] bool apply(trusted::PartitionView& view,
        const trusted::BranchingModel& model, std::string& error) const;
};

struct RestrictCutAngle {
    trusted::CutSegmentId reference;
    double minimum_degrees = 0.0;
    double maximum_degrees = 0.0;

    void apply(trusted::PartitionView& view,
        const trusted::BranchingModel& model,
        trusted::PartitionViewList& result) const;
};

struct RestrictCutDistance {
    trusted::CutSegmentId cut_segment;
    trusted::CutSegmentId reference;
    bool has_minimum = false;
    double minimum_distance = 0.0;
    bool has_maximum = false;
    double maximum_distance = 0.0;

    [[nodiscard]] bool apply(trusted::PartitionView& view,
        const trusted::BranchingModel& model, std::string& error) const;
};

struct RestrictCutDistancePercent {
    trusted::CutSegmentId cut_segment;
    trusted::CutSegmentId reference;
    bool has_minimum = false;
    double minimum_percent = 0.0;
    bool has_maximum = false;
    double maximum_percent = 0.0;
    trusted::CutSegmentId percentage_reference;

    [[nodiscard]] bool apply(trusted::PartitionView& view,
        const trusted::BranchingModel& model, std::string& error) const;
};

struct RestrictDistanceExtra {
    std::int32_t cut_id = -1;
    std::int32_t child_cut_id = -1;
    bool is_left = false;
    double minimum_distance = 0.0;

    [[nodiscard]] bool apply(trusted::PartitionView& view,
        const trusted::BranchingModel& model, std::string& error) const;
};

} // namespace phoenix::partition::adapted
