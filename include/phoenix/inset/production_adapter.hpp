#pragma once

#include "phoenix/geometry.hpp"
#include "phoenix/publication.hpp"

#include <cstdint>
#include <string>

namespace phoenix::inset {

struct Labels {
    LabelId result_face = unassigned_label_id;
    LabelId side_face = unassigned_label_id;
    LabelId result_edge = unassigned_label_id;
    LabelId left_edge = unassigned_label_id;
    LabelId right_edge = unassigned_label_id;
    LabelId top_edge = unassigned_label_id;
    LabelId bottom_edge = unassigned_label_id;
};

struct ProductionInsetRequest {
    FaceReference source;
    double amount = 0.0;
    Labels labels;
};

struct ProductionInsetResult {
    CanonicalGeometryRef geometry;
    FaceId consumed_face_id;
    std::string error;

    [[nodiscard]] bool success() const noexcept
    {
        return geometry != nullptr && error.empty();
    }

    [[nodiscard]] GeometryItemEffect publication_effect(
        std::uint64_t item_key, const ActorId& owner_actor_id) const;
};

// Exact 2D geometry is invocation-local to this call. Only canonical 3D
// geometry and stable scalar IDs cross this API boundary.
[[nodiscard]] ProductionInsetResult run_production_inset(
    const ProductionInsetRequest& request,
    RunElementIdAllocator& ids);

} // namespace phoenix::inset
