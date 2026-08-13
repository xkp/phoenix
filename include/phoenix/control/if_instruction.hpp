#pragma once

#include "phoenix/execution.hpp"

namespace phoenix::control {

struct IfInstructionConfig {
    PortId input_port = "input";
    PortId condition_port = "condition";
    PortId then_port = "then";
    PortId else_port = "else";
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
[[nodiscard]] InstructionHandler make_if_instruction_handler(IfInstructionConfig config = {});

} // namespace phoenix::control
