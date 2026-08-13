#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/merge/production_adapter.hpp"

namespace phoenix::merge {

struct InstructionConfig {
    PortId geometry_input_port = "geometry";
    PortId geometry_output_port = "result";
    Options options;
};

[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);

} // namespace phoenix::merge
