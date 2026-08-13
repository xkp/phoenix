#include "phoenix/control/if_instruction.hpp"

#include <utility>

namespace phoenix::control {
namespace {

const RuntimeValue* input(const InstructionExecutionFrame& frame, const PortId& port)
{
    for (const auto& candidate : frame.inputs.promised_inputs)
        if (candidate.port == port) return &candidate.value;
    return nullptr;
}

} // namespace

TruthinessResult resolve_truthiness(const RuntimeValue& value) noexcept
{
    if (value.is_missing() || value.is_empty() || value.is_defaulted())
        return {TruthinessStatus::missing, false};
    const auto* literal = value.as_literal();
    if (!literal) return {TruthinessStatus::unsupported, false};
    const auto scalar = literal_first_scalar(*literal);
    if (!scalar) return {TruthinessStatus::unsupported, false};
    if (const auto* boolean = std::get_if<bool>(&*scalar))
        return {TruthinessStatus::resolved, *boolean};
    if (const auto* integer = std::get_if<std::int64_t>(&*scalar))
        return {TruthinessStatus::resolved, *integer != 0};
    if (const auto* number = std::get_if<double>(&*scalar))
        return {TruthinessStatus::resolved, *number != 0.0};
    return {TruthinessStatus::unsupported, false};
}

InstructionHandler make_if_instruction_handler(IfInstructionConfig config)
{
    return [config = std::move(config)](const InstructionExecutionFrame& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        const auto* routed = input(frame, config.input_port);
        const auto* condition = input(frame, config.condition_port);
        if (!routed || routed->is_missing()) {
            result.failure_message = "If requires an input value.";
            return result;
        }
        if (!condition) {
            result.failure_message = "If requires a condition value.";
            return result;
        }
        const auto truth = resolve_truthiness(*condition);
        if (truth.status != TruthinessStatus::resolved) {
            result.failure_message = truth.status == TruthinessStatus::missing
                ? "If condition is missing."
                : "If condition must be a boolean or numeric scalar.";
            return result;
        }
        result.produced_outputs.push_back({truth.value ? config.then_port : config.else_port,
            *routed});
        return result;
    };
}

} // namespace phoenix::control
