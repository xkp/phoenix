#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/extrusion/kernel.hpp"

namespace phoenix::extrusion {

struct InstructionConfig {
    PortId geometry_input_port = "geometry";
    PortId geometry_output_port = "result";
    ProfileRef profile;
    LabelId bottom_label = unassigned_label_id;
    LabelId right_label = unassigned_label_id;
    LabelId top_label = unassigned_label_id;
    LabelId left_label = unassigned_label_id;
    LabelId skirt_label = unassigned_label_id;
    LabelId cap_label = unassigned_label_id;
    RepairPolicy repair_policy;
};

[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);

} // namespace phoenix::extrusion
