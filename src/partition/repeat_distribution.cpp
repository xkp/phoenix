#include "phoenix/partition/repeat_distribution.hpp"

#include <algorithm>
#include <cmath>

namespace phoenix::partition::adapted {

RepeatDistribution distribute_repeat_by_count(const RepeatCountInput& input,
    double distance_left, double distance_right)
{
    RepeatDistribution result;
    result.count = input.count;
    auto margin_start = input.margin_start < 0.0
        ? input.secondary : input.margin_start;
    auto margin_end = input.margin_end < 0.0
        ? input.secondary : input.margin_end;
    result.slope.secondary = input.secondary;
    result.slope.margin_start = margin_start;
    result.slope.margin_end = margin_end;
    if (result.count <= 0) {
        result.error = "repeat count must be positive";
        return result;
    }
    result.slope.primary_left = (distance_left - margin_start - margin_end
        - input.secondary * (result.count - 1)) / result.count;
    result.slope.primary_right = (distance_right - margin_start - margin_end
        - input.secondary * (result.count - 1)) / result.count;
    if (result.slope.primary_left < 0.0
        || result.slope.primary_right < 0.0)
        result.error = "not enough space for repeat";
    return result;
}

RepeatDistribution distribute_repeat_by_length(const RepeatLengthInput& input,
    double distance_left, double distance_right)
{
    RepeatDistribution result;
    const auto minimum_distance = (std::min)(distance_left, distance_right);
    const auto maximum_distance = (std::max)(distance_left, distance_right);
    auto secondary = input.secondary;
    auto margin_start = input.margin_start < 0.0 ? secondary : input.margin_start;
    auto margin_end = input.margin_end < 0.0 ? secondary : input.margin_end;
    if (input.primary_length <= 0.0) {
        result.error = "repeat length is missing";
        return result;
    }
    result.count = static_cast<std::int32_t>(std::floor(1e-5
        + (minimum_distance - margin_start - margin_end + secondary)
            / (input.primary_length + secondary) + 1e-5));
    if (input.maximum_cuts > 0 && input.maximum_cuts < result.count)
        result.count = input.maximum_cuts;
    if (result.count <= 0) {
        result.error = "not enough space for repeat";
        return result;
    }
    auto primary = input.primary_length;
    switch (input.adjust_mode) {
    case RepeatAdjustMode::primary:
        primary = (minimum_distance - margin_start - margin_end
            + secondary * (1 - result.count)) / result.count;
        break;
    case RepeatAdjustMode::secondary:
        if (input.margin_start < 0.0 && input.margin_end < 0.0)
            secondary = (minimum_distance - primary * result.count)
                / (result.count + 1);
        else if (input.margin_start >= 0.0 && input.margin_end >= 0.0)
            secondary = result.count == 1 ? -1.0
                : (minimum_distance - primary * result.count
                    - margin_start - margin_end) / (result.count - 1);
        else if (input.margin_start >= 0.0)
            secondary = (minimum_distance - margin_start
                - primary * result.count) / result.count;
        else
            secondary = (minimum_distance - margin_end
                - primary * result.count) / result.count;
        if (secondary >= 0.0 && secondary < 0.001) secondary = 0.0;
        if (input.margin_start < 0.0) margin_start = secondary;
        if (input.margin_end < 0.0) margin_end = secondary;
        break;
    case RepeatAdjustMode::first:
        margin_start = minimum_distance - margin_end + secondary
            - (primary + secondary) * result.count;
        if (margin_start >= 0.0 && margin_start < 0.001) margin_start = 0.0;
        break;
    case RepeatAdjustMode::last:
        margin_end = minimum_distance - margin_start + secondary
            - (primary + secondary) * result.count;
        if (margin_end >= 0.0 && margin_end < 0.001) margin_end = 0.0;
        break;
    case RepeatAdjustMode::extremes:
        margin_start = (minimum_distance + secondary
            - (primary + secondary) * result.count) * 0.5;
        if (margin_start >= 0.0 && margin_start < 0.001) margin_start = 0.0;
        margin_end = margin_start;
        break;
    }
    if (primary < 0.0 || secondary < 0.0 || margin_start < 0.0
        || margin_end < 0.0) {
        result.error = "not enough space for repeat";
        return result;
    }
    const auto primary_maximum = (maximum_distance - margin_start - margin_end
        + secondary * (1 - result.count)) / result.count;
    if (distance_left > distance_right) {
        result.slope.primary_left = primary_maximum;
        result.slope.primary_right = primary;
    } else {
        result.slope.primary_left = primary;
        result.slope.primary_right = primary_maximum;
    }
    result.slope.secondary = secondary;
    result.slope.margin_start = margin_start;
    result.slope.margin_end = margin_end;
    return result;
}

} // namespace phoenix::partition::adapted
