#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/scripting/expression.hpp"
#include "phoenix/scripting/geometry_bindings.hpp"

#include <memory>

namespace phoenix::control {

struct CaseBranch {
    PortId output_port;
    scripting::ExpressionSpec expression;
};

struct CaseInstructionConfig {
    PortId input_port = "input";
    PortId else_port = "else";
    std::vector<CaseBranch> branches;
    std::optional<scripting::GeometryBindingPlan> geometry_bindings;
    std::shared_ptr<const scripting::Engine> expression_engine;
};

[[nodiscard]] std::string configuration_revision(const CaseInstructionConfig& config);
[[nodiscard]] InstructionHandler make_case_instruction_handler(CaseInstructionConfig config);

} // namespace phoenix::control
