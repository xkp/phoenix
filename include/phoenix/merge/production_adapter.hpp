#pragma once

#include "phoenix/geometry.hpp"
#include "phoenix/merge/production_pipeline.hpp"
#include "phoenix/publication.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace phoenix::merge {

struct SourceFace {
    FaceReference face;
    ActorId owner_actor_id;
};

struct ProductionMergeRequest {
    std::vector<SourceFace> sources;
    Options options;
};

struct ProductionMergeResult {
    CanonicalGeometryRef geometry;
    std::vector<SourceFaceIdentity> consumed_faces;
    std::string error;

    [[nodiscard]] bool success() const noexcept
    {
        return geometry != nullptr && error.empty();
    }

    [[nodiscard]] GeometryItemEffect publication_effect(std::uint64_t item_key) const;
};

// Exact CGAL geometry exists only inside this adapter call. Canonical 3D
// geometry and stable scalar IDs are the complete public boundary.
[[nodiscard]] ProductionMergeResult run_production_merge(
    const ProductionMergeRequest& request, RunElementIdAllocator& ids);

} // namespace phoenix::merge
