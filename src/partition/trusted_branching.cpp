#include "phoenix/partition/trusted_branching.hpp"

#include <CGAL/Triangle_2.h>
#include <CGAL/intersections.h>
#include <CGAL/number_utils.h>

#include <algorithm>
#include <cmath>
#include <variant>

namespace phoenix::partition::trusted {

bool ModelFilter::apply(const ArrangementRepoEdge& left,
    const ArrangementRepoEdge& right) const
{
    return reversed ? filter(right, left) : filter(left, right);
}

ModelFilter ModelFilter::reverse() const
{
    return {second, first, filter, !reversed};
}

BranchingModel::BranchingModel(std::int32_t base_segments,
    std::vector<TrustedCut> cuts, std::vector<ModelFilter> filters)
    : base_segments_(base_segments), filters_(std::move(filters))
{
    for (auto& cut : cuts) cuts_.emplace(cut.id, std::move(cut));
}

SegmentInfo* BranchingModel::branch_simple(PartitionView& view,
    CutSegmentId segment, bool check_orientation) const
{
    if (!segment.valid()) return nullptr;
    const auto* candidates = view.repository->get(SegmentRef{segment.value});
    if (candidates == nullptr || candidates->size() != 1) return nullptr;
    const auto candidate = candidates->begin();
    if (view.has_edge(*candidate)
        || (check_orientation && !is_candidate(view, candidate->segment)))
        return nullptr;
    return view.add_segment(SegmentInfo{segment, *candidate});
}

void BranchingModel::branch(PartitionView& view, CutSegmentId segment,
    PartitionViewList& result, bool check_orientation) const
{
    if (segment.empty()) return;
    const ArrangementRepoEdgeList* candidates = nullptr;
    if (is_base_segment(segment)) {
        candidates = view.repository->get(SegmentRef{segment.value});
    } else {
        const auto root = get_root_segment(segment);
        candidates = root.valid()
            ? view.repository->get(SegmentRef{root.value})
            : &view.repository->all_edges();
    }
    if (candidates == nullptr) return;

    const auto segment_filters = filters_for(segment);
    for (const auto& candidate : *candidates) {
        if (view.has_edge(candidate)
            || (check_orientation && !is_candidate(view, candidate.segment)))
            continue;
        bool valid_segment = true;
        for (const auto& filter : segment_filters) {
            const auto* other = view.segment(filter.second);
            if (other != nullptr)
                valid_segment = filter.apply(candidate, other->repository_edge);
            if (!valid_segment) break;
        }
        if (valid_segment)
            result.push_back(view_for_segment(view, segment, candidate));
    }
}

BranchReturnType BranchingModel::branch(PartitionView& view,
    CutSegmentId first, CutSegmentId second, PartitionViewList& result,
    bool check_orientation) const
{
    if (first.empty()) {
        branch(view, second, result, check_orientation);
        return result.empty() ? BranchReturnType::fail_second : BranchReturnType::ok;
    }
    if (second.empty()) {
        branch(view, first, result, check_orientation);
        return result.empty() ? BranchReturnType::fail_first : BranchReturnType::ok;
    }

    PartitionViewList first_views;
    PartitionViewList second_views;
    branch(view, first, first_views, check_orientation);
    if (first_views.empty()) return BranchReturnType::fail_first;
    branch(view, second, second_views, check_orientation);
    if (second_views.empty()) return BranchReturnType::fail_second;

    const auto pair_filters = filters_for(first, second);
    for (auto& first_view : first_views) {
        auto* first_segment = first_view.segment(first);
        for (auto& second_view : second_views) {
            auto* second_segment = second_view.segment(second);
            if (first_segment->repository_edge.has_edge()
                && first_segment->repository_edge == second_segment->repository_edge)
                continue;
            const ExactKernel::Line_2 first_line{
                first_segment->source, first_segment->target};
            if (first_line.has_on(second_segment->source)
                && first_line.has_on(second_segment->target))
                continue;
            bool valid_cut = true;
            for (const auto& filter : pair_filters) {
                valid_cut = filter.apply(first_segment->repository_edge,
                    second_segment->repository_edge);
                if (!valid_cut) break;
            }
            if (valid_cut)
                result.push_back(view_for_segments(view,
                    *first_segment, *second_segment));
        }
    }
    return result.empty() ? BranchReturnType::fail_both : BranchReturnType::ok;
}

void BranchingModel::branch(PartitionView& view, const TrustedCut& cut,
    PartitionViewList& result) const
{
    // Mechanical port of production partition_model::branch(view, cut, result).
    if (view.angles.empty()) return;

    auto* source_info = view.segment(cut.source);
    if (source_info == nullptr) return;
    auto* target_info = view.segment(cut.target);
    if (target_info == nullptr) return;

    auto* source_start = &source_info->source;
    auto* source_end = &source_info->target;
    auto* target_start = &target_info->source;
    auto* target_end = &target_info->target;
    const ExactKernel::Segment_2 source{*source_start, *source_end};
    const ExactKernel::Segment_2 target{*target_start, *target_end};

    const auto source_length = std::sqrt(CGAL::to_double(source.squared_length()));
    const auto target_length = std::sqrt(CGAL::to_double(target.squared_length()));
    auto source_direction = *source_end - *source_start;
    auto target_direction = *target_end - *target_start;
    if (source_length > 0.0)
        source_direction = source_direction / ExactKernel::FT{source_length};
    if (target_length > 0.0)
        target_direction = target_direction / ExactKernel::FT{target_length};

    std::size_t variations = production_cut_variation_count;
    const bool angle_restriction = view.is_angle_restricted();
    const bool source_collapsed = source_length <= 0.01;
    const bool target_collapsed = target_length <= 0.01;
    const bool angle_collapsed = angle_restriction && view.angles.size() == 1
        && view.angles.front().minimum_angle == view.angles.front().maximum_angle;
    if (static_cast<int>(source_collapsed) + static_cast<int>(target_collapsed)
            + static_cast<int>(angle_collapsed) >= 2)
        variations = 1;

    auto angle_ranges = view.angles;
    if (angle_restriction) view.random->shuffle(angle_ranges);

    std::optional<ExactKernel::Line_2> oriented_source_line;
    std::optional<ExactKernel::Line_2> oriented_target_line;
    if (source_info->repository_edge.has_edge())
        oriented_source_line = source_info->repository_edge.segment.supporting_line();
    if (target_info->repository_edge.has_edge())
        oriented_target_line = target_info->repository_edge.segment.supporting_line();

    const auto has_interceptions = [&view, source_info, target_info](
        const ExactPoint2& cut_source, const ExactPoint2& cut_target) {
        if (!view.repository->is_face_concave()) return false;
        const ExactKernel::Segment_2 cut_segment{cut_source, cut_target};
        for (const auto& edge : view.repository->all_edges()) {
            if (edge == source_info->repository_edge
                || edge == target_info->repository_edge)
                continue;
            if (edge.segment.source() == cut_source
                || edge.segment.target() == cut_source
                || edge.segment.source() == cut_target
                || edge.segment.target() == cut_target)
                continue;
            if (edge.segment.has_on(cut_source) || edge.segment.has_on(cut_target))
                continue;
            const auto intersection = CGAL::intersection(cut_segment, edge.segment);
            if (intersection && std::get_if<ExactPoint2>(&*intersection) != nullptr)
                return true;
        }
        return false;
    };

    const auto is_valid_solution = [&source, &target, &oriented_source_line,
        &oriented_target_line](const ExactPoint2& cut_source,
        const ExactPoint2& cut_target) {
        if ((!source.is_degenerate()
                && CGAL::squared_distance(source.supporting_line(), cut_target) < 1e-8)
            || (!target.is_degenerate()
                && CGAL::squared_distance(target.supporting_line(), cut_source) < 1e-8))
            return false;
        if (oriented_target_line
            && oriented_target_line->has_on_negative_side(cut_source))
            return false;
        if (oriented_source_line
            && oriented_source_line->has_on_negative_side(cut_target))
            return false;
        return true;
    };

    for (std::size_t variation = 0; variation < variations; ++variation) {
        bool solution_found = true;
        auto cut_source = *source_start + source_direction
            * ExactKernel::FT{view.random->next() * source_length};
        auto cut_target = *target_start + target_direction
            * ExactKernel::FT{view.random->next() * target_length};

        if (angle_restriction) {
            solution_found = false;
            for (const auto& range : angle_ranges) {
                auto solution = cut_target;
                if (angle_solution(*view.random, cut_source,
                        range.minimum_angle, range.maximum_angle, target, solution)
                    && solution != source.source() && solution != source.target()
                    && is_valid_solution(cut_source, solution)) {
                    if (has_interceptions(cut_source, solution)) continue;
                    cut_target = solution;
                    solution_found = true;
                } else {
                    solution = cut_source;
                    const auto pi = std::acos(-1.0);
                    if (angle_solution(*view.random, cut_target,
                            range.minimum_angle + pi, range.maximum_angle + pi,
                            source, solution)
                        && solution != target.source() && solution != target.target()
                        && is_valid_solution(solution, cut_target)) {
                        if (has_interceptions(solution, cut_target)) continue;
                        cut_source = solution;
                        solution_found = true;
                    } else {
                        continue;
                    }
                }
                if (solution_found) break;
            }
        } else {
            solution_found = is_valid_solution(cut_source, cut_target)
                && !has_interceptions(cut_source, cut_target);
        }
        if (solution_found)
            result.push_back(view_for_cut(
                view, cut, cut_source, cut_target, 0.0));
    }
}

PartitionView BranchingModel::view_for_segment(PartitionView& view,
    CutSegmentId segment, const ArrangementRepoEdge& edge) const
{
    PartitionView result{*view.repository, *view.random};
    result.copy(view);
    result.add_segment(SegmentInfo{segment, edge});
    return result;
}

PartitionView BranchingModel::view_for_segment(PartitionView& view,
    CutSegmentId segment, const SegmentInfo& info) const
{
    PartitionView result{*view.repository, *view.random};
    result.copy(view);
    result.add_segment(SegmentInfo{segment, info.source, info.target});
    return result;
}

PartitionView BranchingModel::view_for_segments(PartitionView& view,
    const SegmentInfo& first, const SegmentInfo& second) const
{
    PartitionView result{*view.repository, *view.random};
    result.copy(view);
    using Triangle = CGAL::Triangle_2<ExactKernel>;
    double total_area = 0.0;
    if (first.source == second.source || first.target == second.source) {
        total_area = std::abs(CGAL::to_double(
            Triangle{first.source, first.target, second.target}.area()));
    } else if (first.source == second.target || first.target == second.target) {
        total_area = std::abs(CGAL::to_double(
            Triangle{first.source, first.target, second.source}.area()));
    } else {
        const auto direction = first.target - first.source;
        const ExactKernel::Vector_2 perpendicular{-direction.y(), direction.x()};
        const ExactKernel::Line_2 line{first.source, perpendicular};
        const auto first_value = line.a() * second.source.x()
            + line.b() * second.source.y() + line.c();
        const auto second_value = line.a() * second.target.x()
            + line.b() * second.target.y() + line.c();
        const auto* second_source = &second.source;
        const auto* second_target = &second.target;
        if (first_value < second_value) std::swap(second_source, second_target);
        total_area = std::abs(CGAL::to_double(
            Triangle{first.source, first.target, *second_source}.area()))
            + std::abs(CGAL::to_double(
                Triangle{first.target, *second_source, *second_target}.area()));
    }
    constexpr double maximum_length = 5000.0;
    result.quality = (std::min)(1.0,
        total_area / (maximum_length * maximum_length));
    const auto first_midpoint = CGAL::midpoint(first.source, first.target);
    const auto second_midpoint = CGAL::midpoint(second.source, second.target);
    result.cut_middle_line = ExactKernel::Line_2{first_midpoint, second_midpoint};
    result.add_segment(first);
    result.add_segment(second);
    return result;
}

PartitionView BranchingModel::view_for_cut(PartitionView& view,
    const TrustedCut& cut, const ExactPoint2& source_point,
    const ExactPoint2& target_point, double quality) const
{
    PartitionView result{*view.repository, *view.random};
    result.copy(view);
    const auto cut_id = cut_segment(cut.id);
    const SegmentInfo cut_info{cut_id, source_point, target_point};
    const auto* source = view.segment(cut.source);
    const auto* target = view.segment(cut.target);

    ExactKernel::Line_2 cut_line{source_point, target_point};
    auto source_line = cut_line;
    auto source_left = source->original_source;
    auto source_right = source->original_target;
    if (source_line.has_on(source_left) && source_line.has_on(source_right)) {
        source_line = ExactKernel::Line_2{source_point,
            target->original_source == target_point
                ? target->original_target : target->original_source};
    }
    if (source_line.has_on_negative_side(source_left)
        || source_line.has_on_positive_side(source_right))
        std::swap(source_left, source_right);

    auto target_left = target->original_source;
    auto target_right = target->original_target;
    if (cut_line.has_on(target_left) && cut_line.has_on(target_right)) {
        cut_line = ExactKernel::Line_2{
            source->original_source == source_point
                ? source->original_target : source->original_source,
            target_point};
    }
    if (cut_line.has_on_negative_side(target_left)
        || cut_line.has_on_positive_side(target_right))
        std::swap(target_left, target_right);

    result.add_segment(cut_info);
    result.add_segment(SegmentInfo{cut_id.cut_result(
        CutSegmentType{CutSegmentKind::source_left}), source_left, source_point});
    result.add_segment(SegmentInfo{cut_id.cut_result(
        CutSegmentType{CutSegmentKind::source_right}), source_point, source_right});
    result.add_segment(SegmentInfo{cut_id.cut_result(
        CutSegmentType{CutSegmentKind::target_left}), target_left, target_point});
    result.add_segment(SegmentInfo{cut_id.cut_result(
        CutSegmentType{CutSegmentKind::target_right}), target_point, target_right});
    result.quality = quality;
    return result;
}

bool BranchingModel::angle_solution(CompatibilityRandomStream& random,
    const ExactPoint2& fixed_point, double minimum_angle,
    double maximum_angle, const ExactKernel::Segment_2& segment,
    ExactPoint2& solution) const
{
    const auto direction_for = [](double angle) {
        return ExactKernel::Vector_2{std::cos(angle), std::sin(angle)};
    };
    const auto point_intersection = [](const auto& first, const auto& second,
        ExactPoint2& output) {
        const auto intersection = CGAL::intersection(first, second);
        if (!intersection) return false;
        const auto* point = std::get_if<ExactPoint2>(&*intersection);
        if (point == nullptr) return false;
        output = *point;
        return true;
    };

    if (std::abs(maximum_angle - minimum_angle) <= 1e-5) {
        const ExactKernel::Line_2 line{fixed_point, direction_for(minimum_angle)};
        if (segment.is_degenerate()) {
            const auto valid = CGAL::squared_distance(line, segment.source()) < 1e-6;
            if (valid) solution = segment.source();
            return valid;
        }
        for (const auto& point : {segment.source(), segment.target()}) {
            if (CGAL::squared_distance(line, point) < 1e-6) {
                solution = point;
                return true;
            }
        }
        return point_intersection(line, segment, solution);
    }

    auto first = segment.source();
    auto second = segment.target();
    ExactKernel::Ray_2 minimum_ray{fixed_point, direction_for(minimum_angle)};
    ExactKernel::Ray_2 maximum_ray{fixed_point, direction_for(maximum_angle)};
    for (std::int32_t pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            minimum_ray = minimum_ray.opposite();
            maximum_ray = maximum_ray.opposite();
        }
        const auto minimum_line = minimum_ray.supporting_line();
        const auto maximum_line = maximum_ray.supporting_line().opposite();
        if (!minimum_line.has_on_negative_side(solution)
            && !maximum_line.has_on_negative_side(solution))
            return true;

        if (minimum_line.has_on_negative_side(first)
            && minimum_line.has_on_negative_side(second)) {
            if (CGAL::squared_distance(first, minimum_line) < 1e-6) {
                solution = first;
                return true;
            }
            if (CGAL::squared_distance(second, minimum_line) < 1e-6) {
                solution = second;
                return true;
            }
            continue;
        }
        if (maximum_line.has_on_negative_side(first)
            && maximum_line.has_on_negative_side(second)) {
            if (CGAL::squared_distance(first, maximum_line) < 1e-6) {
                solution = first;
                return true;
            }
            if (CGAL::squared_distance(second, maximum_line) < 1e-6) {
                solution = second;
                return true;
            }
            continue;
        }

        ExactPoint2 minimum_point;
        ExactPoint2 maximum_point;
        const auto has_minimum = point_intersection(
            minimum_ray, segment, minimum_point);
        const auto has_maximum = point_intersection(
            maximum_ray, segment, maximum_point);
        if (has_minimum && has_maximum) {
            first = minimum_point;
            second = maximum_point;
        } else {
            const auto first_inside = !minimum_line.has_on_negative_side(first)
                && !maximum_line.has_on_negative_side(first);
            const auto second_inside = !minimum_line.has_on_negative_side(second)
                && !maximum_line.has_on_negative_side(second);
            if (!has_minimum && !has_maximum) {
                if (!first_inside || !second_inside) continue;
            } else if (first_inside) {
                second = has_minimum ? minimum_point : maximum_point;
            } else {
                if (!second_inside) continue;
                first = has_minimum ? minimum_point : maximum_point;
            }
        }
        auto direction = second - first;
        const auto length = std::sqrt(CGAL::to_double(direction.squared_length()));
        if (length > 1e-5) direction = direction / ExactKernel::FT{length};
        solution = first + direction * ExactKernel::FT{length * random.next()};
        return true;
    }
    return false;
}

CutSegmentId BranchingModel::cut_segment(std::int32_t cut_id) const
{
    return CutSegmentId{base_segments_ + 5 * cut_id};
}

const TrustedCut* BranchingModel::get_cut(std::int32_t cut_id) const
{
    const auto found = cuts_.find(cut_id);
    return found == cuts_.end() ? nullptr : &found->second;
}

CutSegmentId BranchingModel::get_parent_segment(CutSegmentId segment,
    const TrustedCut* cut) const
{
    std::int32_t cut_index = -1;
    CutSegmentType type;
    if (!is_cut_segment(segment, cut_index, type) || type.is_cut()) return {};
    if (cut == nullptr) cut = get_cut(cut_index);
    return cut == nullptr ? CutSegmentId{}
        : (type.is_source() ? cut->source : cut->target);
}

CutSegmentId BranchingModel::get_root_segment(CutSegmentId segment) const
{
    std::int32_t cut_index = -1;
    CutSegmentType type;
    if (!is_cut_segment(segment, cut_index, type) || type.is_cut()) return {};
    auto* cut = get_cut(cut_index);
    for (auto parent = segment; cut != nullptr;) {
        const auto next = get_parent_segment(segment, cut);
        if (next.empty()) break;
        segment = parent = next;
        cut = cut->parent.has_value() ? get_cut(*cut->parent) : nullptr;
    }
    return segment;
}

bool BranchingModel::is_cut_segment(CutSegmentId segment,
    std::int32_t& cut_index, CutSegmentType& type) const
{
    const auto index = segment.value - base_segments_;
    if (index < 0) {
        cut_index = -1;
        type.set(CutSegmentKind::base);
        return false;
    }
    cut_index = index / 5;
    type.set(index % 5);
    return true;
}

bool BranchingModel::is_cut(CutSegmentId segment) const
{
    const auto index = segment.value - base_segments_;
    return index >= 0 && index % 5 == 0;
}

bool BranchingModel::is_base_segment(CutSegmentId segment) const
{
    return segment.value < base_segments_;
}

bool BranchingModel::is_candidate(const PartitionView& view,
    const ExactKernel::Segment_2& candidate) const
{
    if (view.cut_index < 0) return true;
    const auto* child = get_cut(view.cut_index + 1);
    if (child == nullptr || !child->parent.has_value()) return false;
    auto* cut = get_cut(*child->parent);
    while (cut != nullptr) {
        const auto* segment = view.segment(cut->segment);
        if (segment == nullptr) return false;
        const ExactKernel::Line_2 line{segment->source, segment->target};
        bool valid = true;
        if (cut->right == child->id) {
            valid = !line.has_on_positive_side(candidate.source())
                && !line.has_on_positive_side(candidate.target());
        } else {
            if (cut->left != child->id) return false;
            valid = !line.has_on_negative_side(candidate.source())
                && !line.has_on_negative_side(candidate.target());
        }
        if (!valid) return false;
        child = cut;
        cut = cut->parent.has_value() ? get_cut(*cut->parent) : nullptr;
    }
    return true;
}

std::vector<ModelFilter> BranchingModel::filters_for(CutSegmentId segment) const
{
    std::vector<ModelFilter> result;
    for (const auto& filter : filters_) {
        if (filter.first == segment) result.push_back(filter);
        else if (filter.second == segment) result.push_back(filter.reverse());
    }
    return result;
}

std::vector<ModelFilter> BranchingModel::filters_for(
    CutSegmentId first, CutSegmentId second) const
{
    std::vector<ModelFilter> result;
    for (const auto& filter : filters_) {
        if (filter.first == first && filter.second == second)
            result.push_back(filter);
        else if (filter.first == second && filter.second == first)
            result.push_back(filter.reverse());
    }
    return result;
}

} // namespace phoenix::partition::trusted
