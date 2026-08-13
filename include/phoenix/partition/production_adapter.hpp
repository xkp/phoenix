#pragma once

#include "phoenix/geometry.hpp"
#include "phoenix/publication.hpp"

#include <cstdint>
#include <map>
#include <string>

struct partition_model;

namespace phoenix::partition {

struct ProductionPartitionRequest {
    FaceReference source;
    partition_model* model = nullptr;
    std::map<std::string, double> values;
    std::map<int, LabelId> base_segment_labels;
    std::int32_t random_seed = 1;
};

struct ProductionPartitionResult {
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

[[nodiscard]] ProductionPartitionResult run_production_partition(
    const ProductionPartitionRequest& request,
    RunElementIdAllocator& ids);

} // namespace phoenix::partition
