#include "phoenix/partition/solver_view.hpp"

#include <CGAL/Kernel/global_functions_2.h>

#include <cstddef>
#include <cmath>
#include <set>

namespace phoenix::partition {
namespace {

bool can_merge(const ExactWorkingFace& face, std::size_t left, std::size_t right)
{
    const auto size = face.boundary.size();
    const auto& first = face.boundary[left];
    const auto& second = face.boundary[right];
    return first.current_label == second.current_label
        && CGAL::collinear(first.point, face.boundary[(left + 1) % size].point,
            face.boundary[(right + 1) % size].point);
}

LabelId candidate_label(const SegmentCandidate& candidate, LabelMatchField field)
{
    switch (field) {
    case LabelMatchField::current: return candidate.current_label;
    case LabelMatchField::opposite: return candidate.opposite_label;
    case LabelMatchField::opposite_face: return candidate.opposite_face_label;
    }
    return unassigned_label_id;
}

bool matches(const SegmentCandidate& candidate, const BaseSegmentDefinition& definition)
{
    for (const auto& predicate : definition.predicates) {
        const auto equal = candidate_label(candidate, predicate.field) == predicate.label;
        if ((predicate.comparison == LabelMatchComparison::equal) != equal)
            return false;
    }
    const auto squared_length = candidate.segment.squared_length();
    if (definition.minimum_length.has_value()) {
        const auto minimum = *definition.minimum_length;
        if (squared_length < ExactKernel::FT{minimum * minimum}) return false;
    }
    if (definition.maximum_length.has_value()) {
        const auto maximum = *definition.maximum_length;
        if (squared_length > ExactKernel::FT{maximum * maximum}) return false;
    }
    return true;
}

bool is_concave(const ExactWorkingFace& face)
{
    CGAL::Orientation orientation = CGAL::COLLINEAR;
    const auto size = face.boundary.size();
    for (std::size_t index = 0; index < size; ++index) {
        const auto turn = CGAL::orientation(face.boundary[index].point,
            face.boundary[(index + 1) % size].point,
            face.boundary[(index + 2) % size].point);
        if (turn == CGAL::COLLINEAR) continue;
        if (orientation == CGAL::COLLINEAR) orientation = turn;
        else if (turn != orientation) return true;
    }
    return false;
}

} // namespace

std::optional<SegmentRepository> SegmentRepository::from_boundary(
    const ExactWorkingFace& face, const PartitionPlan& plan,
    std::string& error)
{
    error.clear();
    if (face.boundary.size() < 3) {
        error = "partition repository requires a closed boundary";
        return std::nullopt;
    }

    SegmentRepository repository;
    const auto size = face.boundary.size();
    std::size_t start = 0;
    while (start < size && can_merge(face, (start + size - 1) % size, start))
        ++start;
    if (start == size) {
        error = "partition boundary cannot be one collinear segment";
        return std::nullopt;
    }

    std::vector<SegmentCandidate> groups;
    std::size_t consumed = 0;
    while (consumed < size) {
        const auto group_start = (start + consumed) % size;
        std::size_t group_size = 1;
        while (consumed + group_size < size
            && can_merge(face, (group_start + group_size - 1) % size,
                (group_start + group_size) % size)) {
            ++group_size;
        }
        const auto& boundary = face.boundary[group_start];
        const auto group_end = (group_start + group_size) % size;
        SegmentCandidate candidate{
            ExactKernel::Segment_2{boundary.point, face.boundary[group_end].point},
            {}, boundary.source_vertex_id, boundary.source_halfedge_id,
            boundary.source_edge_id, boundary.current_label,
            boundary.opposite_label, boundary.opposite_face_label};
        for (std::size_t offset = 0; offset < group_size; ++offset)
            candidate.boundary_indices.push_back((group_start + offset) % size);
        groups.push_back(std::move(candidate));
        consumed += group_size;
    }
    repository.boundary_segments_ = groups;
    repository.face_concave_ = is_concave(face);

    if (plan.base_segments().size() != static_cast<std::size_t>(plan.base_segment_count())) {
        error = "partition plan must define every base segment match";
        return std::nullopt;
    }
    std::set<SegmentRef> defined_segments;
    for (const auto& definition : plan.base_segments()) {
        if (definition.id.value() < 0
            || definition.id.value() >= plan.base_segment_count()
            || !defined_segments.insert(definition.id).second) {
            error = "partition base segment definitions must have unique in-range IDs";
            return std::nullopt;
        }
        if ((definition.minimum_length.has_value()
                && (!std::isfinite(*definition.minimum_length)
                    || *definition.minimum_length < 0.0))
            || (definition.maximum_length.has_value()
                && (!std::isfinite(*definition.maximum_length)
                    || *definition.maximum_length < 0.0))
            || (definition.minimum_length.has_value()
                && definition.maximum_length.has_value()
                && *definition.minimum_length > *definition.maximum_length)) {
            error = "partition base segment length range is invalid";
            return std::nullopt;
        }
        auto& candidates = repository.candidates_[definition.id];
        for (const auto& group : groups) {
            if (matches(group, definition)) candidates.push_back(group);
        }
    }
    return repository;
}

const std::vector<SegmentCandidate>* SegmentRepository::candidates(
    SegmentRef segment) const noexcept
{
    const auto found = candidates_.find(segment);
    return found == candidates_.end() ? nullptr : &found->second;
}

void SelectedSegment::reset() noexcept
{
    source = original_source;
    target = original_target;
}

bool SolverView::select_unique(
    SegmentRef segment, bool reject_reused_boundary, std::string& error)
{
    error.clear();
    if (selected_.find(segment) != selected_.end()) {
        return true;
    }
    const auto* matches = repository_->candidates(segment);
    if (matches == nullptr || matches->size() != 1) {
        error = "partition base segment must resolve to exactly one boundary candidate";
        return false;
    }
    const auto& candidate = matches->front();
    if (reject_reused_boundary) {
        for (const auto boundary_index : candidate.boundary_indices) {
            if (!contains_boundary(boundary_index)) continue;
            error = "partition source and target resolved to the same boundary candidate";
            return false;
        }
    }
    selected_.emplace(segment, SelectedSegment{
        segment,
        candidate.segment.source(),
        candidate.segment.target(),
        candidate.segment.source(),
        candidate.segment.target(),
        &candidate,
    });
    return true;
}

const SelectedSegment* SolverView::selected(SegmentRef segment) const noexcept
{
    const auto found = selected_.find(segment);
    return found == selected_.end() ? nullptr : &found->second;
}

SelectedSegment* SolverView::selected_mutable(SegmentRef segment) noexcept
{
    const auto found = selected_.find(segment);
    return found == selected_.end() ? nullptr : &found->second;
}

bool SolverView::contains_boundary(std::size_t boundary_index) const noexcept
{
    for (const auto& entry : selected_) {
        if (entry.second.candidate == nullptr) continue;
        for (const auto selected_index : entry.second.candidate->boundary_indices)
            if (selected_index == boundary_index) return true;
    }
    return false;
}

void SolverView::put_working_segment(
    SegmentRef segment, ExactPoint2 source, ExactPoint2 target)
{
    selected_.erase(segment);
    selected_.emplace(segment,
        SelectedSegment{segment, source, target, source, target, nullptr});
}

void SolverView::reset() noexcept
{
    for (auto& entry : selected_) {
        entry.second.reset();
    }
}

std::optional<CutSeed> select_cut_seed(
    SolverView& view, const CutDefinition& cut, std::string& error)
{
    if (cut.source() == cut.target()) {
        error = "partition source and target cannot reference the same segment";
        return std::nullopt;
    }
    if (!view.select_unique(cut.source(), true, error)
        || !view.select_unique(cut.target(), true, error)) {
        return std::nullopt;
    }
    return CutSeed{view.selected(cut.source()), view.selected(cut.target())};
}

} // namespace phoenix::partition
