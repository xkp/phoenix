#include "phoenix/partition/straight_cut_tessellator.hpp"

#include <CGAL/Arr_observer.h>

namespace phoenix::partition::adapted {
namespace {

using Halfedge = ExactArrangement::Halfedge_handle;
using Vertex = ExactArrangement::Vertex_handle;

void apply_label(LabelId label, Halfedge edge)
{
    if (label.is_registered()) edge->data().label = label;
}

Halfedge find_directed(ExactArrangement& arrangement,
    const ExactKernel::Segment_2& segment)
{
    for (auto edge = arrangement.halfedges_begin();
         edge != arrangement.halfedges_end(); ++edge) {
        if (edge->source()->point() == segment.source()
            && edge->target()->point() == segment.target())
            return edge;
    }
    return {};
}

Halfedge find_boundary_edge(ExactArrangement::Face_handle face,
    const ExactPoint2& point)
{
    if (face == ExactArrangement::Face_handle{} || face->is_unbounded()
        || face->number_of_outer_ccbs() == 0)
        return {};
    for (auto ccb = face->outer_ccbs_begin(); ccb != face->outer_ccbs_end(); ++ccb) {
        auto edge = *ccb;
        const auto first = edge;
        do {
            const ExactKernel::Segment_2 segment{
                edge->source()->point(), edge->target()->point()};
            if (segment.has_on(point)) return edge;
            edge = edge->next();
        } while (edge != first);
    }
    return {};
}

bool boundary_contains(ExactArrangement::Face_handle face,
    const ExactPoint2& first, const ExactPoint2& second)
{
    return find_boundary_edge(face, first) != Halfedge{}
        && find_boundary_edge(face, second) != Halfedge{};
}

bool collinear_with(const ExactKernel::Segment_2& reference,
    const ExactKernel::Segment_2& candidate)
{
    if (reference.is_degenerate()) return false;
    const auto line = reference.supporting_line();
    return line.has_on(candidate.source()) && line.has_on(candidate.target());
}

void apply_repeat_region_labels(ExactArrangement::Face_handle face,
    const ExactKernel::Segment_2& lower,
    const ExactKernel::Segment_2& upper,
    const ExactKernel::Segment_2& source_boundary,
    const ExactKernel::Segment_2& target_boundary,
    const RepeatRegionLabels& labels)
{
    if (labels.face.is_registered()) face->data().label = labels.face;
    auto edge = face->outer_ccb();
    const auto first = edge;
    do {
        const ExactKernel::Segment_2 segment{
            edge->source()->point(), edge->target()->point()};
        if (collinear_with(lower, segment)) {
            Halfedge handle = edge;
            apply_label(labels.lower_edge, handle);
        } else if (collinear_with(upper, segment)) {
            Halfedge handle = edge;
            apply_label(labels.upper_edge, handle);
        } else if (collinear_with(source_boundary, segment)) {
            Halfedge handle = edge;
            apply_label(labels.source_side, handle);
            apply_label(labels.source_side_opposite, handle->twin());
        } else if (collinear_with(target_boundary, segment)) {
            Halfedge handle = edge;
            apply_label(labels.target_side, handle);
            apply_label(labels.target_side_opposite, handle->twin());
        }
        ++edge;
    } while (edge != first);
}

Vertex split_at(ExactArrangement& arrangement, Halfedge edge,
    const ExactPoint2& point)
{
    if (edge->source()->point() == point) return edge->source();
    if (edge->target()->point() == point) return edge->target();
    const auto source = edge->source()->point();
    const auto target = edge->target()->point();
    auto first = arrangement.split_edge(edge,
        ExactKernel::Segment_2{source, point},
        ExactKernel::Segment_2{point, target});
    return first->target();
}

} // namespace

TessellationResult StraightCutTessellator::tessellate(
    ExactArrangement& arrangement, ExactArrangement::Face_handle face,
    const trusted::PartitionView& view, const trusted::TrustedCut& cut,
    const CutLabels& labels) const
{
    TessellationResult result;
    const auto* cut_segment = view.segment(cut.segment);
    const auto* source = view.segment(cut.source);
    const auto* target = view.segment(cut.target);
    if (cut_segment == nullptr || source == nullptr || target == nullptr) {
        result.error = "straight cut requires source and target segments";
        return result;
    }
    const auto original_face_data = face->data();
    auto source_edge = find_boundary_edge(face, cut_segment->source);
    auto target_edge = find_boundary_edge(face, cut_segment->target);
    if (source_edge == Halfedge{} || target_edge == Halfedge{}) {
        result.error = "cut endpoints are not on the current face boundary";
        return result;
    }
    const ExactKernel::Segment_2 source_curve{
        source_edge->curve().source(), source_edge->curve().target()};
    const ExactKernel::Segment_2 target_curve{
        target_edge->curve().source(), target_edge->curve().target()};
    if (!source_curve.has_on(cut_segment->source)
        || !target_curve.has_on(cut_segment->target)) {
        result.error = "cut endpoints do not lie on selected boundary edges";
        return result;
    }
    const auto source_vertex = split_at(
        arrangement, source_edge, cut_segment->source);
    const auto target_vertex = split_at(
        arrangement, target_edge, cut_segment->target);
    if (source_vertex == target_vertex) {
        result.error = "cut collapsed to one boundary vertex";
        return result;
    }

    const auto label_piece = [&arrangement](const trusted::SegmentInfo* piece,
        LabelId current, LabelId opposite) {
        if (piece == nullptr) return false;
        if (piece->segment().is_degenerate()) return true;
        auto edge = find_directed(arrangement, piece->segment());
        if (edge == Halfedge{}) return false;
        apply_label(current, edge);
        apply_label(opposite, edge->twin());
        return true;
    };
    const auto cut_id = cut.segment;
    const auto* source_left = view.segment(cut_id.cut_result(
        trusted::CutSegmentType{trusted::CutSegmentKind::source_left}));
    const auto* source_right = view.segment(cut_id.cut_result(
        trusted::CutSegmentType{trusted::CutSegmentKind::source_right}));
    const auto* target_left = view.segment(cut_id.cut_result(
        trusted::CutSegmentType{trusted::CutSegmentKind::target_left}));
    const auto* target_right = view.segment(cut_id.cut_result(
        trusted::CutSegmentType{trusted::CutSegmentKind::target_right}));
    if (!label_piece(source_left,
            labels.source_left, labels.source_left_opposite)
        || !label_piece(source_right,
            labels.source_right, labels.source_right_opposite)
        || !label_piece(target_left,
            labels.target_left, labels.target_left_opposite)
        || !label_piece(target_right,
            labels.target_right, labels.target_right_opposite)) {
        result.error = "cut boundary pieces do not match arrangement topology";
        return result;
    }

    const bool left_collapsed = source_left->segment().is_degenerate()
        && target_left->segment().is_degenerate();
    const bool right_collapsed = source_right->segment().is_degenerate()
        && target_right->segment().is_degenerate();
    if (left_collapsed && right_collapsed) {
        result.error = "both sides of the cut collapsed";
        return result;
    }
    if (left_collapsed || right_collapsed) {
        auto existing = find_directed(arrangement, cut_segment->segment());
        if (existing == Halfedge{})
            existing = find_directed(arrangement, cut_segment->segment().opposite());
        if (existing == Halfedge{}) {
            result.error = "collapsed cut does not match an existing boundary";
            return result;
        }
        if (existing->source()->point() != cut_segment->source)
            existing = existing->twin();
        result.cut = existing;
        apply_label(labels.cut_left, existing);
        apply_label(labels.cut_right, existing->twin());
        auto surviving_face = existing->face()->is_unbounded()
            ? existing->twin()->face() : existing->face();
        surviving_face->data() = original_face_data;
        const auto surviving_label = left_collapsed
            ? labels.face_right : labels.face_left;
        if (surviving_label.is_registered())
            surviving_face->data().label = surviving_label;
        result.faces = {surviving_face};
        if (left_collapsed) result.right_face = surviving_face;
        else result.left_face = surviving_face;
        return result;
    }

    result.cut = arrangement.insert_at_vertices(
        cut_segment->segment(), source_vertex, target_vertex);
    apply_label(labels.cut_left, result.cut);
    apply_label(labels.cut_right, result.cut->twin());
    result.cut->face()->data() = original_face_data;
    result.cut->twin()->face()->data() = original_face_data;
    if (labels.face_left.is_registered())
        result.cut->face()->data().label = labels.face_left;
    if (labels.face_right.is_registered())
        result.cut->twin()->face()->data().label = labels.face_right;
    result.faces = {result.cut->face(), result.cut->twin()->face()};
    result.left_face = result.cut->face();
    result.right_face = result.cut->twin()->face();
    return result;
}

TessellationResult StraightCutTessellator::tessellate_tree(
    ExactArrangement& arrangement, ExactArrangement::Face_handle face,
    const trusted::PartitionView& view, const trusted::BranchingModel& model,
    std::int32_t root_cut_id,
    const std::map<std::int32_t, CutLabels>& labels) const
{
    TessellationResult result;
    const auto recurse = [&](const auto& self, std::int32_t cut_id,
        ExactArrangement::Face_handle current_face,
        std::vector<ExactArrangement::Face_handle>& leaves,
        Halfedge& first_cut, std::string& error) -> bool {
        const auto* cut = model.get_cut(cut_id);
        const auto label_entry = labels.find(cut_id);
        if (cut == nullptr || label_entry == labels.end()) {
            error = "recursive cut or its labels are unavailable";
            return false;
        }
        auto current = tessellate(
            arrangement, current_face, view, *cut, label_entry->second);
        if (!current.success()) {
            error = current.error;
            return false;
        }
        if (first_cut == Halfedge{}) first_cut = current.cut;
        if (current.left_face != ExactArrangement::Face_handle{}
            && cut->left.has_value()) {
            if (!self(self, *cut->left, current.left_face,
                    leaves, first_cut, error))
                return false;
        } else if (current.left_face != ExactArrangement::Face_handle{}) {
            leaves.push_back(current.left_face);
        }
        if (current.right_face != ExactArrangement::Face_handle{}
            && cut->right.has_value()) {
            if (!self(self, *cut->right, current.right_face,
                    leaves, first_cut, error))
                return false;
        } else if (current.right_face != ExactArrangement::Face_handle{}) {
            leaves.push_back(current.right_face);
        }
        return true;
    };
    if (!recurse(recurse, root_cut_id, face, result.faces,
            result.cut, result.error)) {
        result.faces.clear();
    }
    return result;
}

TessellationResult StraightCutTessellator::tessellate_repeat_interpolated(
    ExactArrangement& arrangement, ExactArrangement::Face_handle face,
    const ExactKernel::Segment_2& source_boundary,
    const ExactKernel::Segment_2& target_boundary,
    const RepeatDistribution& distribution,
    const RepeatTessellationLabels& labels) const
{
    TessellationResult result;
    if (!distribution.success()) {
        result.error = distribution.error.empty()
            ? "repeat distribution is invalid" : distribution.error;
        return result;
    }
    const auto source_length = std::sqrt(
        CGAL::to_double(source_boundary.squared_length()));
    const auto target_length = std::sqrt(
        CGAL::to_double(target_boundary.squared_length()));
    if (source_length <= 0.0 || target_length <= 0.0) {
        result.error = "interpolated repeat boundaries must not collapse";
        return result;
    }
    const auto source_direction = source_boundary.to_vector()
        / ExactKernel::FT{source_length};
    const auto target_direction = target_boundary.to_vector()
        / ExactKernel::FT{target_length};
    auto source_point = source_boundary.source();
    auto target_point = target_boundary.source();
    auto current_face = face;
    auto lower_source_point = source_point;
    auto lower_target_point = target_point;

    struct BoundarySpec {
        double source_advance;
        double target_advance;
        const RepeatRegionLabels* region;
    };
    std::vector<BoundarySpec> boundaries;
    for (std::int32_t index = 0; index < distribution.count; ++index) {
        if ((index == 0 && distribution.slope.margin_start > 0.0)
            || (index > 0 && distribution.slope.secondary > 0.0)) {
            const auto advance = index == 0
                ? distribution.slope.margin_start : distribution.slope.secondary;
            boundaries.push_back({advance, advance,
                index == 0 ? &labels.margin_start : &labels.secondary});
        }
        const bool final_without_margin = index == distribution.count - 1
            && distribution.slope.margin_end == 0.0;
        if (!final_without_margin)
            boundaries.push_back({distribution.slope.primary_left,
                distribution.slope.primary_right, &labels.primary});
    }

    for (std::size_t index = 0; index < boundaries.size(); ++index) {
        const auto& boundary = boundaries[index];
        source_point = source_point + source_direction
            * ExactKernel::FT{boundary.source_advance};
        target_point = target_point + target_direction
            * ExactKernel::FT{boundary.target_advance};
        auto source_edge = find_boundary_edge(current_face, source_point);
        auto target_edge = find_boundary_edge(current_face, target_point);
        if (source_edge == Halfedge{} || target_edge == Halfedge{}) {
            result.error = "repeat chord endpoints left the current face boundary";
            return result;
        }
        const auto source_vertex = split_at(arrangement, source_edge, source_point);
        const auto target_vertex = split_at(arrangement, target_edge, target_point);
        auto chord = arrangement.insert_at_vertices(
            ExactKernel::Segment_2{source_point, target_point},
            source_vertex, target_vertex);
        if (result.cut == Halfedge{}) result.cut = chord;

        ExactArrangement::Face_handle remaining;
        ExactArrangement::Face_handle emitted;
        const auto has_next = index + 1 < boundaries.size();
        if (has_next) {
            const auto next_source = source_point + source_direction
                * ExactKernel::FT{boundaries[index + 1].source_advance};
            const auto next_target = target_point + target_direction
                * ExactKernel::FT{boundaries[index + 1].target_advance};
            if (boundary_contains(chord->face(), next_source, next_target)) {
                remaining = chord->face();
                emitted = chord->twin()->face();
            } else {
                remaining = chord->twin()->face();
                emitted = chord->face();
            }
        } else {
            remaining = chord->face();
            emitted = chord->twin()->face();
            if (!boundary_contains(remaining,
                    target_boundary.target(), source_boundary.target()))
                std::swap(remaining, emitted);
        }
        apply_repeat_region_labels(emitted,
            {lower_source_point, lower_target_point},
            {source_point, target_point}, source_boundary, target_boundary,
            *boundary.region);
        apply_label(boundary.region->upper_edge, chord);
        apply_label(boundary.region->upper_edge, chord->twin());
        result.faces.push_back(emitted);
        current_face = remaining;
        lower_source_point = source_point;
        lower_target_point = target_point;
    }
    const auto& final_labels = distribution.slope.margin_end > 0.0
        ? labels.margin_end : labels.primary;
    apply_repeat_region_labels(current_face,
        {lower_source_point, lower_target_point},
        {source_boundary.target(), target_boundary.target()},
        source_boundary, target_boundary, final_labels);
    result.faces.push_back(current_face);
    result.left_face = current_face;
    return result;
}

} // namespace phoenix::partition::adapted
