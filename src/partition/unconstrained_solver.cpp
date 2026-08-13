#include "phoenix/partition/unconstrained_solver.hpp"
#include "phoenix/partition/angle_ranges.hpp"
#include "phoenix/partition/sampling.hpp"

#include <CGAL/intersections.h>
#include <CGAL/squared_distance_2.h>

#include <cmath>
#include <variant>

namespace phoenix::partition {
namespace {

ExactPoint2 interpolate(const ExactPoint2& source, const ExactPoint2& target,
    double fraction)
{
    const ExactKernel::FT exact_fraction{fraction};
    return source + (target - source) * exact_fraction;
}

bool shares_boundary(const SegmentCandidate& candidate, const SelectedSegment& selected)
{
    for (const auto left : candidate.boundary_indices)
        for (const auto right : selected.candidate->boundary_indices)
            if (left == right) return true;
    return false;
}

bool has_interception(const SegmentRepository& repository,
    const SelectedSegment& source, const SelectedSegment& target,
    const ExactPoint2& source_point, const ExactPoint2& target_point)
{
    if (!repository.is_face_concave()) return false;
    const ExactKernel::Segment_2 cut{source_point, target_point};
    for (const auto& edge : repository.boundary_segments()) {
        if (shares_boundary(edge, source) || shares_boundary(edge, target)) continue;
        if (edge.segment.has_on(source_point) || edge.segment.has_on(target_point)) continue;
        if (CGAL::do_intersect(cut, edge.segment)) return true;
    }
    return false;
}

bool valid_solution(const SegmentRepository& repository,
    const SelectedSegment& source, const SelectedSegment& target,
    const ExactPoint2& source_point, const ExactPoint2& target_point)
{
    const auto tolerance = ExactKernel::FT{1e-8};
    const auto source_segment = ExactKernel::Segment_2{source.source, source.target};
    const auto target_segment = ExactKernel::Segment_2{target.source, target.target};
    if ((!source_segment.is_degenerate()
            && CGAL::squared_distance(source_segment.supporting_line(), target_point)
                < tolerance)
        || (!target_segment.is_degenerate()
            && CGAL::squared_distance(target_segment.supporting_line(), source_point)
                < tolerance)) {
        return false;
    }
    if (!target.candidate->segment.is_degenerate()
        && target.candidate->segment.supporting_line().has_on_negative_side(source_point))
        return false;
    if (!source.candidate->segment.is_degenerate()
        && source.candidate->segment.supporting_line().has_on_negative_side(target_point))
        return false;
    return !has_interception(repository, source, target, source_point, target_point);
}

bool fixed_angle_solution(const ExactPoint2& fixed_point,
    const ExactKernel::Segment_2& segment, double radians, ExactPoint2& solution)
{
    const ExactKernel::Vector_2 direction{std::cos(radians), std::sin(radians)};
    const ExactKernel::Line_2 line{fixed_point, direction};
    const auto tolerance = ExactKernel::FT{1e-6};
    for (const auto& endpoint : {segment.source(), segment.target()}) {
        if (CGAL::squared_distance(line, endpoint) < tolerance) {
            solution = endpoint;
            return true;
        }
    }
    const auto intersection = CGAL::intersection(line, segment);
    if (!intersection) return false;
    const auto* point = std::get_if<ExactPoint2>(&*intersection);
    if (point == nullptr) return false;
    solution = *point;
    return true;
}

bool intersection_point(const ExactKernel::Ray_2& ray,
    const ExactKernel::Segment_2& segment, ExactPoint2& point)
{
    const auto intersection = CGAL::intersection(ray, segment);
    if (!intersection) return false;
    const auto* result = std::get_if<ExactPoint2>(&*intersection);
    if (result == nullptr) return false;
    point = *result;
    return true;
}

bool cone_solution(CompatibilityRandomStream& random,
    const ExactPoint2& fixed_point, const AngleRange& range,
    const ExactKernel::Segment_2& segment, ExactPoint2& solution)
{
    if (std::abs(range.maximum - range.minimum) <= 1e-5)
        return fixed_angle_solution(fixed_point, segment, range.minimum, solution);

    auto first = segment.source();
    auto second = segment.target();
    const auto tolerance = ExactKernel::FT{1e-6};
    for (int pass = 0; pass < 2; ++pass) {
        const auto sign = pass == 0 ? 1.0 : -1.0;
        const ExactKernel::Vector_2 minimum_direction{
            sign * std::cos(range.minimum), sign * std::sin(range.minimum)};
        const ExactKernel::Vector_2 maximum_direction{
            sign * std::cos(range.maximum), sign * std::sin(range.maximum)};
        const ExactKernel::Ray_2 minimum_ray{fixed_point, minimum_direction};
        const ExactKernel::Ray_2 maximum_ray{fixed_point, maximum_direction};
        const auto minimum_line = minimum_ray.supporting_line();
        const auto maximum_line = maximum_ray.supporting_line().opposite();

        if (!minimum_line.has_on_negative_side(solution)
            && !maximum_line.has_on_negative_side(solution))
            return true;

        if (minimum_line.has_on_negative_side(first)
            && minimum_line.has_on_negative_side(second)) {
            if (CGAL::squared_distance(first, minimum_line) < tolerance) {
                solution = first;
                return true;
            }
            if (CGAL::squared_distance(second, minimum_line) < tolerance) {
                solution = second;
                return true;
            }
            continue;
        }
        if (maximum_line.has_on_negative_side(first)
            && maximum_line.has_on_negative_side(second)) {
            if (CGAL::squared_distance(first, maximum_line) < tolerance) {
                solution = first;
                return true;
            }
            if (CGAL::squared_distance(second, maximum_line) < tolerance) {
                solution = second;
                return true;
            }
            continue;
        }

        ExactPoint2 minimum_intersection;
        ExactPoint2 maximum_intersection;
        const auto has_minimum = intersection_point(
            minimum_ray, segment, minimum_intersection);
        const auto has_maximum = intersection_point(
            maximum_ray, segment, maximum_intersection);
        if (has_minimum && has_maximum) {
            first = minimum_intersection;
            second = maximum_intersection;
        } else {
            const auto first_inside = !minimum_line.has_on_negative_side(first)
                && !maximum_line.has_on_negative_side(first);
            const auto second_inside = !minimum_line.has_on_negative_side(second)
                && !maximum_line.has_on_negative_side(second);
            if (!has_minimum && !has_maximum) {
                if (!first_inside || !second_inside) continue;
            } else if (first_inside) {
                second = has_minimum ? minimum_intersection : maximum_intersection;
            } else {
                if (!second_inside) continue;
                first = has_minimum ? minimum_intersection : maximum_intersection;
            }
        }
        solution = interpolate(first, second, random.next());
        return true;
    }
    return false;
}

} // namespace

FixedAngleRestriction fixed_angle_from_reference(
    const SelectedSegment& reference, double offset_degrees)
{
    const auto vector = reference.original_target - reference.original_source;
    auto reference_angle = std::atan2(
        CGAL::to_double(vector.y()), CGAL::to_double(vector.x()));
    const auto pi = std::acos(-1.0);
    if (reference_angle < 0.0) reference_angle += pi;
    return {reference_angle + offset_degrees * pi / 180.0};
}

CutSolveResult solve_unconstrained_cut(const SegmentRepository& repository,
    const CutSeed& seed, const std::vector<CutPointSample>& samples,
    std::optional<FixedAngleRestriction> fixed_angle)
{
    CutSolveResult result;
    if (seed.source == nullptr || seed.target == nullptr
        || seed.source->candidate == nullptr || seed.target->candidate == nullptr) {
        result.error = "unconstrained partition cut requires two boundary segments";
        return result;
    }
    for (const auto& sample : samples) {
        if (!std::isfinite(sample.source_fraction)
            || !std::isfinite(sample.target_fraction)
            || sample.source_fraction < 0.0 || sample.source_fraction > 1.0
            || sample.target_fraction < 0.0 || sample.target_fraction > 1.0) {
            result.error = "partition cut fractions must be finite values in [0, 1]";
            return result;
        }
        auto source_point = interpolate(
            seed.source->source, seed.source->target, sample.source_fraction);
        auto target_point = interpolate(
            seed.target->source, seed.target->target, sample.target_fraction);
        if (fixed_angle.has_value()) {
            const auto target_segment = ExactKernel::Segment_2{
                seed.target->source, seed.target->target};
            if (!fixed_angle_solution(source_point, target_segment,
                    fixed_angle->radians, target_point)) {
                const auto source_segment = ExactKernel::Segment_2{
                    seed.source->source, seed.source->target};
                if (!fixed_angle_solution(target_point, source_segment,
                        fixed_angle->radians, source_point)) {
                    continue;
                }
            }
        }
        if (valid_solution(repository, *seed.source, *seed.target,
                source_point, target_point)) {
            result.solutions.push_back({source_point, target_point});
        }
    }
    if (result.solutions.empty() && result.error.empty())
        result.error = "partition cut samples produced no valid solution";
    return result;
}

CutSolveResult solve_angle_restricted_cut(const SegmentRepository& repository,
    const CutSeed& seed, CompatibilityRandomStream& random,
    const AngleRangeSet& angles, std::size_t variation_count)
{
    CutSolveResult result;
    if (!angles.restricted() || angles.ranges().empty()) {
        result.error = "partition angle solver requires a non-empty restriction";
        return result;
    }
    if (seed.source == nullptr || seed.target == nullptr) {
        result.error = "partition angle solver requires source and target segments";
        return result;
    }
    auto ranges = angles.ranges();
    random.shuffle(ranges);
    for (std::size_t variation = 0; variation < variation_count; ++variation) {
        auto source_point = interpolate(seed.source->source, seed.source->target, random.next());
        auto target_point = interpolate(seed.target->source, seed.target->target, random.next());
        bool found = false;
        for (const auto& range : ranges) {
            auto proposed_target = target_point;
            if (cone_solution(random, source_point, range,
                    ExactKernel::Segment_2{seed.target->source, seed.target->target},
                    proposed_target)
                && valid_solution(repository, *seed.source, *seed.target,
                    source_point, proposed_target)) {
                target_point = proposed_target;
                found = true;
                break;
            }
            auto proposed_source = source_point;
            AngleRange reverse{range.minimum + std::acos(-1.0),
                range.maximum + std::acos(-1.0)};
            if (cone_solution(random, target_point, reverse,
                    ExactKernel::Segment_2{seed.source->source, seed.source->target},
                    proposed_source)
                && valid_solution(repository, *seed.source, *seed.target,
                    proposed_source, target_point)) {
                source_point = proposed_source;
                found = true;
                break;
            }
        }
        if (found) result.solutions.push_back({source_point, target_point});
    }
    if (result.solutions.empty())
        result.error = "partition angle ranges produced no valid solution";
    return result;
}

SolverView apply_cut_solution(const SolverView& source_view,
    const CutDefinition& cut, const CutSolution& solution)
{
    SolverView result = source_view;
    const auto* source = source_view.selected(cut.source());
    const auto* target = source_view.selected(cut.target());
    const auto base = cut.result().value();

    auto source_left = source->original_source;
    auto source_right = source->original_target;
    auto cut_line = ExactKernel::Line_2{solution.source_point, solution.target_point};
    auto source_line = cut_line;
    if (source_line.has_on(source_left) && source_line.has_on(source_right)) {
        source_line = ExactKernel::Line_2{solution.source_point,
            target->original_source == solution.target_point
                ? target->original_target : target->original_source};
    }
    if (source_line.has_on_negative_side(source_left)
        || source_line.has_on_positive_side(source_right))
        std::swap(source_left, source_right);

    auto target_left = target->original_source;
    auto target_right = target->original_target;
    if (cut_line.has_on(target_left) && cut_line.has_on(target_right)) {
        cut_line = ExactKernel::Line_2{
            source->original_source == solution.source_point
                ? source->original_target : source->original_source,
            solution.target_point};
    }
    if (cut_line.has_on_negative_side(target_left)
        || cut_line.has_on_positive_side(target_right))
        std::swap(target_left, target_right);

    result.put_working_segment(SegmentRef{base},
        solution.source_point, solution.target_point);
    result.put_working_segment(SegmentRef{base + 1}, source_left, solution.source_point);
    result.put_working_segment(SegmentRef{base + 2}, solution.source_point, source_right);
    result.put_working_segment(SegmentRef{base + 3}, target_left, solution.target_point);
    result.put_working_segment(SegmentRef{base + 4}, solution.target_point, target_right);
    return result;
}

} // namespace phoenix::partition
