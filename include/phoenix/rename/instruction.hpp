#pragma once

#include "phoenix/execution.hpp"

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace phoenix::rename {

enum class Target { faces, directed_edges };
enum class LengthKind { none, any, largest, smallest };
enum class AdjacentRelation { any, previous, next };

struct Condition {
    Target target = Target::faces;
    std::optional<LabelId> from_label;
    LabelId to_label = unassigned_label_id;
    bool to_owning_face_label = false;
    std::optional<LabelId> owning_face_label;
    std::optional<LabelId> owning_edge_label;
    std::optional<LabelId> opposite_face_label;
    std::optional<LabelId> opposite_edge_label;
    std::optional<LabelId> adjacent_label1;
    std::optional<LabelId> adjacent_label2;
    AdjacentRelation adjacent_relation = AdjacentRelation::any;
    std::optional<bool> border;
    std::optional<std::size_t> maximum_edge_count;
    double minimum_length = 0.0;
    double maximum_length = std::numeric_limits<double>::max();
    LengthKind length_kind = LengthKind::none;
};

struct InstructionConfig {
    PortId geometry_input_port = "geometry";
    PortId geometry_output_port = "output";
    std::map<LabelId, std::vector<LabelId>> label_map;
    std::optional<LabelId> all_faces_label;
    std::optional<LabelId> all_edges_label;
    std::vector<Condition> conditions;
    std::uint64_t seed = 0;
};

[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);

} // namespace phoenix::rename
