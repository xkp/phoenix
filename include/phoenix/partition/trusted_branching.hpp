#pragma once

#include "phoenix/partition/trusted_solver_foundation.hpp"

#include <functional>
#include <optional>

namespace phoenix::partition::trusted {

struct TrustedCut {
    std::int32_t id = -1;
    CutSegmentId segment;
    CutSegmentId source;
    CutSegmentId target;
    std::optional<std::int32_t> parent;
    std::optional<std::int32_t> left;
    std::optional<std::int32_t> right;
};

using EdgePairFilter = std::function<bool(
    const ArrangementRepoEdge&, const ArrangementRepoEdge&)>;

struct ModelFilter {
    CutSegmentId first;
    CutSegmentId second;
    EdgePairFilter filter;
    bool reversed = false;

    [[nodiscard]] bool apply(const ArrangementRepoEdge& left,
        const ArrangementRepoEdge& right) const;
    [[nodiscard]] ModelFilter reverse() const;
};

enum class BranchReturnType {
    ok = 0,
    fail_first = 1,
    fail_second = 2,
    fail_both = 3,
};

using PartitionViewList = std::vector<PartitionView>;

class BranchingModel {
public:
    BranchingModel(std::int32_t base_segments, std::vector<TrustedCut> cuts,
        std::vector<ModelFilter> filters = {});

    [[nodiscard]] SegmentInfo* branch_simple(PartitionView& view,
        CutSegmentId segment, bool check_orientation) const;
    void branch(PartitionView& view, CutSegmentId segment,
        PartitionViewList& result, bool check_orientation) const;
    [[nodiscard]] BranchReturnType branch(PartitionView& view,
        CutSegmentId first, CutSegmentId second, PartitionViewList& result,
        bool check_orientation) const;
    void branch(PartitionView& view, const TrustedCut& cut,
        PartitionViewList& result) const;

    [[nodiscard]] PartitionView view_for_segment(PartitionView& view,
        CutSegmentId segment, const ArrangementRepoEdge& edge) const;
    [[nodiscard]] PartitionView view_for_segment(PartitionView& view,
        CutSegmentId segment, const SegmentInfo& info) const;
    [[nodiscard]] PartitionView view_for_segments(PartitionView& view,
        const SegmentInfo& first, const SegmentInfo& second) const;
    [[nodiscard]] PartitionView view_for_cut(PartitionView& view,
        const TrustedCut& cut, const ExactPoint2& source_point,
        const ExactPoint2& target_point, double quality) const;
    [[nodiscard]] bool angle_solution(CompatibilityRandomStream& random,
        const ExactPoint2& fixed_point, double minimum_angle,
        double maximum_angle, const ExactKernel::Segment_2& segment,
        ExactPoint2& solution) const;

    [[nodiscard]] CutSegmentId cut_segment(std::int32_t cut_id) const;
    [[nodiscard]] const TrustedCut* get_cut(std::int32_t cut_id) const;
    [[nodiscard]] CutSegmentId get_parent_segment(CutSegmentId segment,
        const TrustedCut* cut = nullptr) const;
    [[nodiscard]] CutSegmentId get_root_segment(CutSegmentId segment) const;
    [[nodiscard]] bool is_cut_segment(CutSegmentId segment,
        std::int32_t& cut_index, CutSegmentType& type) const;
    [[nodiscard]] bool is_cut(CutSegmentId segment) const;
    [[nodiscard]] bool is_base_segment(CutSegmentId segment) const;
    [[nodiscard]] bool is_candidate(const PartitionView& view,
        const ExactKernel::Segment_2& segment) const;

private:
    [[nodiscard]] std::vector<ModelFilter> filters_for(CutSegmentId segment) const;
    [[nodiscard]] std::vector<ModelFilter> filters_for(
        CutSegmentId first, CutSegmentId second) const;

    std::int32_t base_segments_;
    std::map<std::int32_t, TrustedCut> cuts_;
    std::vector<ModelFilter> filters_;
};

} // namespace phoenix::partition::trusted
