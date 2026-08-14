#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/scripting/expression.hpp"
#include "phoenix/scripting/geometry_bindings.hpp"

#include <memory>

namespace phoenix::control {

struct IfInstructionConfig {
    PortId input_port = "input";
    PortId condition_port = "condition";
    PortId then_port = "then";
    PortId else_port = "else";
    std::optional<scripting::ExpressionSpec> expression;
    std::optional<scripting::GeometryBindingPlan> geometry_bindings;
    std::shared_ptr<const scripting::Engine> expression_engine;
};

enum class TruthinessStatus {
    resolved,
    missing,
    unsupported,
};

struct TruthinessResult {
    TruthinessStatus status = TruthinessStatus::missing;
    bool value = false;
};

[[nodiscard]] TruthinessResult resolve_truthiness(const RuntimeValue& value) noexcept;
[[nodiscard]] std::string configuration_revision(const IfInstructionConfig& config);
[[nodiscard]] InstructionHandler make_if_instruction_handler(IfInstructionConfig config = {});

} // namespace phoenix::control
