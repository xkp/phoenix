#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/scripting/expression.hpp"

#include <optional>
#include <string>

namespace phoenix::scripting {

struct NumericValue {
    double literal = 0.0;
    std::optional<ExpressionSpec> expression;
};

struct NumericRange {
    NumericValue minimum;
    std::optional<NumericValue> maximum;
    std::optional<NumericValue> step;
};

struct NumericEvaluation {
    double value = 0.0;
    std::optional<std::string> error;
};

[[nodiscard]] std::string legacy_numeric_expression_source(std::string source);
[[nodiscard]] ExpressionSpec numeric_expression(
    std::string source,
    const Bindings& global_bindings = {});
[[nodiscard]] NumericValue numeric_literal(double value);
[[nodiscard]] NumericValue numeric_expression_value(
    std::string source,
    const Bindings& global_bindings = {},
    double fallback = 0.0);

[[nodiscard]] NumericEvaluation evaluate_numeric(
    const NumericValue& value,
    const InstructionExecutionFrame& frame,
    const Engine* engine = nullptr);
[[nodiscard]] NumericEvaluation evaluate_numeric_range(
    const NumericRange& range,
    const InstructionExecutionFrame& frame,
    const Engine* engine = nullptr);
[[nodiscard]] std::string numeric_value_revision(
    const NumericValue& value,
    const Engine& engine);
[[nodiscard]] std::string numeric_range_revision(
    const NumericRange& range,
    const Engine& engine);

} // namespace phoenix::scripting
