#include "phoenix/partition/constraints.hpp"

#include <CGAL/number_utils.h>

#include <algorithm>
#include <cmath>

namespace phoenix::partition {
namespace {

ExactPoint2 midpoint(const SelectedSegment& segment)
{
    return CGAL::midpoint(segment.source, segment.target);
}

ExactKernel::FT parameter(const SelectedSegment& segment, const ExactPoint2& point)
{
    const auto direction = segment.original_target - segment.original_source;
    return ((point - segment.original_source) * direction) / direction.squared_length();
}

ExactPoint2 at_parameter(const SelectedSegment& segment, const ExactKernel::FT& value)
{
    return segment.original_source
        + (segment.original_target - segment.original_source) * value;
}

} // namespace

bool apply_segment_length_restriction(SolverView& view,
    const SegmentLengthRestriction& restriction, std::string& error)
{
    error.clear();
    if (!std::isfinite(restriction.minimum_length)
        || !std::isfinite(restriction.maximum_length)
        || restriction.minimum_length < 0.0
        || restriction.maximum_length < restriction.minimum_length) {
        error = "partition segment length restriction is invalid";
        return false;
    }
    auto* source = view.selected_mutable(restriction.source_segment);
    auto* target = view.selected_mutable(restriction.target_segment);
    if (source == nullptr || target == nullptr) {
        error = "partition segment length restriction requires selected source and target";
        return false;
    }
    auto* segment = restriction.constrain_source ? source : target;
    const auto squared_length = (segment->original_target
        - segment->original_source).squared_length();
    if (squared_length == ExactKernel::FT{0}) {
        error = "partition segment length restriction cannot constrain a point";
        return false;
    }
    const auto length = std::sqrt(CGAL::to_double(squared_length));
    if (restriction.minimum_length > length) {
        error = "partition segment is shorter than the required length";
        return false;
    }

    const ExactKernel::Line_2 testing_line{midpoint(*source), midpoint(*target)};
    const auto source_is_right =
        testing_line.has_on_negative_side(segment->original_source);
    const auto reversed = !(restriction.left_piece ^ source_is_right);

    auto minimum_parameter = ExactKernel::FT{restriction.minimum_length / length};
    auto maximum_parameter = ExactKernel::FT{
        std::min(restriction.maximum_length, length) / length};
    if (reversed) {
        const auto reversed_minimum = ExactKernel::FT{1} - maximum_parameter;
        const auto reversed_maximum = ExactKernel::FT{1} - minimum_parameter;
        minimum_parameter = reversed_minimum;
        maximum_parameter = reversed_maximum;
    }

    minimum_parameter = (std::max)(minimum_parameter, parameter(*segment, segment->source));
    maximum_parameter = (std::min)(maximum_parameter, parameter(*segment, segment->target));
    if (minimum_parameter > maximum_parameter) {
        error = "partition segment length restrictions produced an empty range";
        return false;
    }
    segment->source = at_parameter(*segment, minimum_parameter);
    segment->target = at_parameter(*segment, maximum_parameter);
    return true;
}

} // namespace phoenix::partition
