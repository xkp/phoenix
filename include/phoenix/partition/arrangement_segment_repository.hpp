#pragma once

#include "phoenix/partition/plan.hpp"
#include "phoenix/partition/working_arrangement.hpp"

#include <map>
#include <string>
#include <vector>

namespace phoenix::partition {

class CompatibilityRandomStream;

// Direct adaptation of production backend/segment_repository.h. Names differ
// only to coexist temporarily with the quarantined compile spike.
struct ArrangementRepoEdge {
    ExactKernel::Segment_2 segment;
    ExactArrangement::Halfedge_handle halfedge_start;
    ExactArrangement::Halfedge_handle halfedge_end;

    ArrangementRepoEdge() = default;
    explicit ArrangementRepoEdge(ExactArrangement::Halfedge_handle halfedge);
    ArrangementRepoEdge(ExactKernel::Segment_2 segment_value,
        ExactArrangement::Halfedge_handle start,
        ExactArrangement::Halfedge_handle end);

    [[nodiscard]] bool operator==(const ArrangementRepoEdge& other) const noexcept;
    [[nodiscard]] bool has_edge() const noexcept;
    void set_label(LabelId label);
    void set_opposite_label(LabelId label);
};

using ArrangementRepoEdgeList = std::vector<ArrangementRepoEdge>;

class ArrangementSegmentRepository {
public:
    [[nodiscard]] bool search(ExactArrangement::Face_handle face,
        const std::vector<BaseSegmentDefinition>& conditions,
        std::string& error);
    void clear();

    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
    [[nodiscard]] bool has(SegmentRef id) const noexcept;
    [[nodiscard]] bool has_conditions(SegmentRef id) const noexcept;
    [[nodiscard]] const ArrangementRepoEdgeList* get(SegmentRef id) const noexcept;
    [[nodiscard]] const ArrangementRepoEdgeList& all_edges() const noexcept { return edges_; }
    [[nodiscard]] const ArrangementRepoEdgeList& unmatched() const noexcept { return unmatched_; }
    [[nodiscard]] bool is_face_concave() const noexcept { return is_face_concave_; }
    void randomize(CompatibilityRandomStream& random);

private:
    [[nodiscard]] ExactKernel::Segment_2 get_collinear_range(
        ExactArrangement::Halfedge_handle source,
        ExactArrangement::Halfedge_handle& start,
        ExactArrangement::Halfedge_handle& end) const;
    void add(SegmentRef id, const ArrangementRepoEdge& edge);

    bool is_face_concave_ = false;
    ArrangementRepoEdgeList edges_;
    std::map<SegmentRef, ArrangementRepoEdgeList> items_;
    std::map<SegmentRef, bool> known_;
    ArrangementRepoEdgeList unmatched_;
};

} // namespace phoenix::partition
