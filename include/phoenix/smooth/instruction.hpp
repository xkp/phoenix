#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/smooth/subdivision.hpp"

namespace phoenix::smooth {

struct InstructionConfig {
    PortId geometry_input_port = "geometry";
    PortId geometry_output_port = "result";
    SubdivisionOptions options;
};

[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);

} // namespace phoenix::smooth
