#pragma once

#include "phoenix/loop/geometry_transaction.hpp"
#include "phoenix/scripting/numeric_value.hpp"

#include <memory>

namespace phoenix::loop {

struct InstructionConfig {
    PortId geometry_input_port = "input";
    PortId geometry_output_port = "output";
    Options options;
    FunctionBodyRequest body;
    TraceSink* trace_sink = nullptr;
    std::optional<scripting::VariablePlan> variables;
    scripting::NumericRange count;
    std::shared_ptr<const scripting::Engine> expression_engine;
};

[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);
[[nodiscard]] std::string configuration_revision(
    const Options& options, const FunctionId& body_function_id);
[[nodiscard]] std::string configuration_revision(const InstructionConfig& config);

} // namespace phoenix::loop
