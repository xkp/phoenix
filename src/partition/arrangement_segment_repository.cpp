#include "phoenix/partition/arrangement_segment_repository.hpp"
#include "phoenix/partition/sampling.hpp"

#include <CGAL/number_utils.h>

#include <cmath>
#include <set>
#include <random>

namespace phoenix::partition {
namespace {

LabelId label_for(const ExactArrangement::Halfedge_handle& halfedge,
    LabelMatchField field)
{
    switch (field) {
    case LabelMatchField::current: return halfedge->data().label;
    case LabelMatchField::opposite: return halfedge->twin()->data().label;
    case LabelMatchField::opposite_face:
        // Phoenix boundary adaptation: projected mesh boundaries have one CGAL
        // exterior face, so the source adjacent-face label is carried by the
        // directed DCEL halfedge rather than inferred from that shared face.
        return halfedge->data().source_opposite_face_label;
    }
    return unassigned_label_id;
}

bool matches(ExactArrangement::Halfedge_handle halfedge,
    const BaseSegmentDefinition& condition)
{
    for (const auto& predicate : condition.predicates) {
        const auto equal = label_for(halfedge, predicate.field) == predicate.label;
        if ((predicate.comparison == LabelMatchComparison::equal) != equal)
            return false;
    }
    // Production length predicates inspect seg.he_start, not the grouped range.
    const auto squared_length = ExactKernel::Segment_2{
        halfedge->source()->point(), halfedge->target()->point()}.squared_length();
    if (condition.minimum_length.has_value()) {
        const auto value = *condition.minimum_length;
        if (squared_length < ExactKernel::FT{value * value}) return false;
    }
    if (condition.maximum_length.has_value()) {
        const auto value = *condition.maximum_length;
        if (squared_length > ExactKernel::FT{value * value}) return false;
    }
    return true;
}

bool face_is_concave(ExactArrangement::Face_handle face)
{
    auto edge = face->outer_ccb();
    const auto end = edge;
    CGAL::Orientation orientation = CGAL::COLLINEAR;
    do {
        const auto turn = CGAL::orientation(edge->source()->point(),
            edge->target()->point(), edge->next()->target()->point());
        if (turn != CGAL::COLLINEAR) {
            if (orientation == CGAL::COLLINEAR) orientation = turn;
            else if (turn != orientation) return true;
        }
        ++edge;
    } while (edge != end);
    return false;
}

} // namespace

ArrangementRepoEdge::ArrangementRepoEdge(ExactArrangement::Halfedge_handle halfedge)
    : segment(halfedge->curve()), halfedge_start(halfedge), halfedge_end(halfedge)
{
}

ArrangementRepoEdge::ArrangementRepoEdge(ExactKernel::Segment_2 segment_value,
    ExactArrangement::Halfedge_handle start, ExactArrangement::Halfedge_handle end)
    : segment(std::move(segment_value)), halfedge_start(start), halfedge_end(end)
{
}

bool ArrangementRepoEdge::operator==(const ArrangementRepoEdge& other) const noexcept
{
    return halfedge_start == other.halfedge_start && halfedge_end == other.halfedge_end;
}

bool ArrangementRepoEdge::has_edge() const noexcept
{
    return halfedge_start != ExactArrangement::Halfedge_handle{};
}

void ArrangementRepoEdge::set_label(LabelId label)
{
    for (auto halfedge = halfedge_start; halfedge != halfedge_end;
         halfedge = halfedge->next())
        halfedge->data().label = label;
}

void ArrangementRepoEdge::set_opposite_label(LabelId label)
{
    for (auto halfedge = halfedge_start; halfedge != halfedge_end;
         halfedge = halfedge->next())
        halfedge->twin()->data().label = label;
}

void ArrangementSegmentRepository::clear()
{
    is_face_concave_ = false;
    items_.clear();
    edges_.clear();
    known_.clear();
    unmatched_.clear();
}

ExactKernel::Segment_2 ArrangementSegmentRepository::get_collinear_range(
    ExactArrangement::Halfedge_handle source,
    ExactArrangement::Halfedge_handle& start,
    ExactArrangement::Halfedge_handle& end) const
{
    const auto label = source->data().label;
    const ExactKernel::Line_2 line{source->source()->point(), source->target()->point()};

    auto edge = source->prev();
    while (edge->data().label == label && line.has_on(edge->source()->point()))
        edge = edge->prev();
    start = edge->next();
    const auto range_source = edge->target()->point();

    edge = source->next();
    while (edge->data().label == label && line.has_on(edge->target()->point()))
        edge = edge->next();
    end = edge;
    const auto range_target = edge->source()->point();
    return {range_source, range_target};
}

bool ArrangementSegmentRepository::search(ExactArrangement::Face_handle face,
    const std::vector<BaseSegmentDefinition>& conditions, std::string& error)
{
    clear();
    error.clear();
    if (face == ExactArrangement::Face_handle{} || face->is_unbounded()) {
        error = "partition segment repository requires a bounded arrangement face";
        return false;
    }
    std::set<SegmentRef> ids;
    for (const auto& condition : conditions) {
        if (!ids.insert(condition.id).second) {
            error = "partition segment repository received a duplicate base segment";
            return false;
        }
        known_[condition.id] = true;
    }
    is_face_concave_ = face_is_concave(face);

    std::vector<ArrangementRepoEdge> segments;
    ExactArrangement::Halfedge_handle start;
    ExactArrangement::Halfedge_handle end;
    ExactArrangement::Halfedge_handle initial;
    const auto first = face->outer_ccb();
    initial = first;
    auto segment = get_collinear_range(initial, start, end);
    initial = start;
    segments.emplace_back(segment, start, end);
    while (end != initial) {
        segment = get_collinear_range(end, start, end);
        segments.emplace_back(segment, start, end);
    }

    for (const auto& candidate : segments) {
        edges_.push_back(candidate);
        bool found = false;
        for (const auto& condition : conditions) {
            if (matches(candidate.halfedge_start, condition)) {
                add(condition.id, candidate);
                found = true;
            }
        }
        if (!found) unmatched_.push_back(candidate);
    }
    return true;
}

bool ArrangementSegmentRepository::has(SegmentRef id) const noexcept
{
    const auto found = items_.find(id);
    return found != items_.end() && !found->second.empty();
}

bool ArrangementSegmentRepository::has_conditions(SegmentRef id) const noexcept
{
    // Preserved production behavior despite the misleading method name.
    return items_.find(id) == items_.end();
}

const ArrangementRepoEdgeList* ArrangementSegmentRepository::get(
    SegmentRef id) const noexcept
{
    const auto found = items_.find(id);
    if (found != items_.end()) return &found->second;
    return known_.find(id) == known_.end() ? &edges_ : nullptr;
}

void ArrangementSegmentRepository::add(SegmentRef id,
    const ArrangementRepoEdge& edge)
{
    items_[id].push_back(edge);
}

void ArrangementSegmentRepository::randomize(CompatibilityRandomStream& random)
{
    // Direct production correspondence: consume one value, seed one standard
    // engine, then reuse that engine across every matched list and all edges.
    std::default_random_engine engine{
        static_cast<unsigned long>(random.next() * 5489U)};
    for (auto& item : items_)
        std::shuffle(item.second.begin(), item.second.end(), engine);
    std::shuffle(edges_.begin(), edges_.end(), engine);
}

} // namespace phoenix::partition
