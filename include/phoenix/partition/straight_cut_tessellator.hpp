#pragma once

// QUARANTINED BEHAVIORAL SCAFFOLDING. Do not add tessellation features here;
// replace this file with mechanically adapted partition_tesselator.{h,cpp}.

#include "phoenix/partition/plan.hpp"
#include "phoenix/partition/trusted_branching.hpp"
#include "phoenix/partition/repeat_distribution.hpp"

#include <string>
#include <map>
#include <vector>

namespace phoenix::partition::adapted {

struct TessellationResult {
    ExactArrangement::Halfedge_handle cut;
    ExactArrangement::Face_handle left_face;
    ExactArrangement::Face_handle right_face;
    std::vector<ExactArrangement::Face_handle> faces;
    std::string error;
    [[nodiscard]] bool success() const noexcept
    { return cut != ExactArrangement::Halfedge_handle{} && error.empty(); }
};

struct RepeatRegionLabels {
    LabelId face = unassigned_label_id;
    LabelId lower_edge = unassigned_label_id;
    LabelId upper_edge = unassigned_label_id;
    LabelId source_side = unassigned_label_id;
    LabelId source_side_opposite = unassigned_label_id;
    LabelId target_side = unassigned_label_id;
    LabelId target_side_opposite = unassigned_label_id;
};

struct RepeatTessellationLabels {
    RepeatRegionLabels primary;
    RepeatRegionLabels secondary;
    RepeatRegionLabels margin_start;
    RepeatRegionLabels margin_end;
};

class StraightCutTessellator {
public:
    [[nodiscard]] TessellationResult tessellate(ExactArrangement& arrangement,
        ExactArrangement::Face_handle face, const trusted::PartitionView& view,
        const trusted::TrustedCut& cut, const CutLabels& labels) const;

    [[nodiscard]] TessellationResult tessellate_tree(
        ExactArrangement& arrangement, ExactArrangement::Face_handle face,
        const trusted::PartitionView& view,
        const trusted::BranchingModel& model, std::int32_t root_cut_id,
        const std::map<std::int32_t, CutLabels>& labels) const;

    [[nodiscard]] TessellationResult tessellate_repeat_interpolated(
        ExactArrangement& arrangement, ExactArrangement::Face_handle face,
        const ExactKernel::Segment_2& source_boundary,
        const ExactKernel::Segment_2& target_boundary,
        const RepeatDistribution& distribution,
        const RepeatTessellationLabels& labels) const;
};

} // namespace phoenix::partition::adapted
