#pragma once

#include "phoenix/execution.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace phoenix::select {

struct LabelRoute {
    LabelId label = unassigned_label_id;
    PortId output = "output";
};

struct FaceCondition {
    std::optional<LabelId> face_label;
    std::optional<LabelId> edge_label;
    std::optional<LabelId> opposite_face_label;
    std::optional<LabelId> opposite_edge_label;
    bool require_border_edge = false;
    double minimum_edge_length = 0.0;
    double maximum_edge_length = std::numeric_limits<double>::max();
};

struct Limit {
    std::int64_t count = -1;
    std::int64_t random_range = -1;
    std::int64_t random_step = 1;
    bool percentage = false;
    std::uint64_t seed = 0;
};

struct InstructionConfig {
    PortId geometry_input_port = "geometry";
    PortId default_output_port = "output";
    PortId else_output_port = "else";
    std::vector<LabelRoute> label_routes;
    std::vector<FaceCondition> conditions;
    Limit limit;
};

[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);

} // namespace phoenix::select
