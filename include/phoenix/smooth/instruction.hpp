#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/scripting/numeric_value.hpp"
#include "phoenix/smooth/subdivision.hpp"

#include <optional>
#include <string>

namespace phoenix::smooth {

struct InstructionConfig {
    PortId geometry_input_port = "geometry";
    PortId geometry_output_port = "result";
    SubdivisionOptions options;
    scripting::NumericValue max_refinement_level =
        scripting::numeric_literal(2.0);
    std::optional<std::string> unsupported_runtime_reason;
};

[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);

} // namespace phoenix::smooth
