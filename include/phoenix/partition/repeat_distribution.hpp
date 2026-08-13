#pragma once

// QUARANTINED BEHAVIORAL SCAFFOLDING. Repeat math remains a fixture oracle
// until the production tessellator source is compiled behind adapters.

#include <cstdint>
#include <string>

namespace phoenix::partition::adapted {

enum class RepeatAdjustMode {
    primary,
    secondary,
    extremes,
    first,
    last,
};

struct RepeatSlope {
    double primary_left = -1.0;
    double primary_right = -1.0;
    double secondary = -1.0;
    double margin_start = -1.0;
    double margin_end = -1.0;
};

struct RepeatCountInput {
    std::int32_t count = 0;
    double secondary = 0.0;
    double margin_start = -1.0;
    double margin_end = -1.0;
};

struct RepeatLengthInput {
    double primary_length = 0.0;
    double secondary = 0.0;
    double margin_start = -1.0;
    double margin_end = -1.0;
    std::int32_t maximum_cuts = 0;
    RepeatAdjustMode adjust_mode = RepeatAdjustMode::primary;
};

struct RepeatDistribution {
    std::int32_t count = 0;
    RepeatSlope slope;
    std::string error;
    [[nodiscard]] bool success() const noexcept
    { return count > 0 && error.empty(); }
};

[[nodiscard]] RepeatDistribution distribute_repeat_by_count(
    const RepeatCountInput& input, double distance_left,
    double distance_right);
[[nodiscard]] RepeatDistribution distribute_repeat_by_length(
    const RepeatLengthInput& input, double distance_left,
    double distance_right);

} // namespace phoenix::partition::adapted
