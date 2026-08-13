#pragma once

// QUARANTINED COMPILE SPIKE: behavioral scaffolding only. The accepted P9 port
// must replace this detached topology with the production segment repository
// operating directly on ExactArrangement handles. See PORTING_PARTITION_SOURCE_MAP.md.

#include "phoenix/partition/plan.hpp"
#include "phoenix/partition/working_face.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phoenix::partition {

struct SegmentCandidate {
    ExactKernel::Segment_2 segment;
    std::vector<std::size_t> boundary_indices;
    VertexId source_vertex_id;
    HalfedgeId source_halfedge_id;
    EdgeId source_edge_id;
    LabelId current_label = unassigned_label_id;
    LabelId opposite_label = unassigned_label_id;
    LabelId opposite_face_label = unassigned_label_id;
};

class SegmentRepository {
public:
    [[nodiscard]] static std::optional<SegmentRepository> from_boundary(
        const ExactWorkingFace& face, const PartitionPlan& plan,
        std::string& error);

    [[nodiscard]] const std::vector<SegmentCandidate>* candidates(
        SegmentRef segment) const noexcept;
    [[nodiscard]] const std::vector<SegmentCandidate>& boundary_segments() const noexcept
    {
        return boundary_segments_;
    }
    [[nodiscard]] bool is_face_concave() const noexcept { return face_concave_; }

private:
    std::map<SegmentRef, std::vector<SegmentCandidate>> candidates_;
    std::vector<SegmentCandidate> boundary_segments_;
    bool face_concave_ = false;
};

struct SelectedSegment {
    SegmentRef id;
    ExactPoint2 source;
    ExactPoint2 target;
    ExactPoint2 original_source;
    ExactPoint2 original_target;
    const SegmentCandidate* candidate = nullptr;

    void reset() noexcept;
    [[nodiscard]] bool collapsed() const noexcept { return source == target; }
};

class SolverView {
public:
    explicit SolverView(const SegmentRepository& repository) noexcept
        : repository_(&repository)
    {
    }

    [[nodiscard]] bool select_unique(
        SegmentRef segment, bool reject_reused_boundary, std::string& error);
    [[nodiscard]] const SelectedSegment* selected(SegmentRef segment) const noexcept;
    [[nodiscard]] SelectedSegment* selected_mutable(SegmentRef segment) noexcept;
    [[nodiscard]] bool contains_boundary(std::size_t boundary_index) const noexcept;
    void put_working_segment(SegmentRef segment, ExactPoint2 source, ExactPoint2 target);
    void reset() noexcept;

private:
    const SegmentRepository* repository_;
    std::map<SegmentRef, SelectedSegment> selected_;
};

struct CutSeed {
    const SelectedSegment* source = nullptr;
    const SelectedSegment* target = nullptr;
};

[[nodiscard]] std::optional<CutSeed> select_cut_seed(
    SolverView& view, const CutDefinition& cut, std::string& error);

} // namespace phoenix::partition
