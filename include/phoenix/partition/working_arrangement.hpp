#pragma once

#include "phoenix/partition/working_face.hpp"

#include <CGAL/Arr_extended_dcel.h>
#include <CGAL/Arr_segment_traits_2.h>
#include <CGAL/Arrangement_2.h>

#include <optional>
#include <string>

namespace phoenix::partition {

struct ArrangementVertexData {
    std::int64_t working_id = -1;
    std::int64_t index = -1;
    std::int64_t tag = -1;
    std::optional<VertexId> source_vertex_id;
};

struct ArrangementHalfedgeData {
    std::int64_t working_id = -1;
    std::int64_t tag = -1;
    std::optional<HalfedgeId> source_halfedge_id;
    std::optional<EdgeId> source_edge_id;
    LabelId label = unassigned_label_id;
    LabelId source_opposite_face_label = unassigned_label_id;
};

struct ArrangementFaceData {
    std::int64_t working_id = -1;
    std::int64_t tag = -1;
    std::optional<FaceId> source_face_id;
    LabelId label = unassigned_label_id;
};

using ExactArrangementTraits = CGAL::Arr_segment_traits_2<ExactKernel>;
using ExactArrangementDcel = CGAL::Arr_extended_dcel<ExactArrangementTraits,
    ArrangementVertexData, ArrangementHalfedgeData, ArrangementFaceData>;
using ExactArrangement = CGAL::Arrangement_2<ExactArrangementTraits, ExactArrangementDcel>;

struct WorkingArrangement {
    PlanarFrame frame;
    ExactArrangement arrangement;
};

struct ArrangementBuildResult {
    std::optional<WorkingArrangement> working;
    std::string error;
    [[nodiscard]] bool success() const noexcept { return working.has_value(); }
};

class ExactArrangementBuilder {
public:
    [[nodiscard]] ArrangementBuildResult build(const ExactWorkingFace& face) const;
};

} // namespace phoenix::partition
