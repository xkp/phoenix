#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/inset/production_adapter.hpp"
#include "phoenix/scripting/numeric_value.hpp"

namespace phoenix::inset {

struct InstructionConfig {
    PortId geometry_input_port = "geometry";
    PortId geometry_output_port = "result";
    scripting::NumericRange amount;
    Labels labels;
};

[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);

} // namespace phoenix::inset
