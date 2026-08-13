#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/inset/production_adapter.hpp"

namespace phoenix::inset {

struct InstructionConfig {
    PortId geometry_input_port = "geometry";
    PortId geometry_output_port = "result";
    double amount = 0.0;
    Labels labels;
};

[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);

} // namespace phoenix::inset
