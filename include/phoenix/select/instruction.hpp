#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/scripting/numeric_value.hpp"

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
    scripting::NumericValue minimum_edge_length = scripting::numeric_literal(0.0);
    scripting::NumericValue maximum_edge_length =
        scripting::numeric_literal(std::numeric_limits<double>::max());
};

struct Limit {
    scripting::NumericRange count = {
        scripting::numeric_literal(-1.0),
        std::nullopt,
        std::nullopt};
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
