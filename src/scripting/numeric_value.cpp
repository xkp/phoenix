#include "phoenix/scripting/numeric_value.hpp"

#include "phoenix/scripting/quickjs_engine.hpp"

#include <cmath>
#include <cstdint>
#include <random>
#include <regex>
#include <sstream>
#include <variant>

namespace phoenix::scripting {
namespace {

const Engine& default_engine()
{
    static const QuickJsEngine engine;
    return engine;
}

Bindings locals_from_frame(const InstructionExecutionFrame& frame)
{
    return frame.function_variables ? *frame.function_variables : Bindings{};
}

std::optional<double> scalar_to_double(const Value& value)
{
    if (const auto* number = std::get_if<double>(&value)) return *number;
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
    }
    if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? 1.0 : 0.0;
    return std::nullopt;
}

} // namespace

std::string legacy_numeric_expression_source(std::string source)
{
    return std::regex_replace(source, std::regex(R"(\[([A-Za-z_][A-Za-z0-9_]*)\])"), "$1");
}

ExpressionSpec numeric_expression(std::string source, const Bindings& global_bindings)
{
    ExpressionSpec result;
    result.program.source = legacy_numeric_expression_source(std::move(source));
    result.global_bindings = global_bindings;
    return result;
}

NumericValue numeric_literal(double value)
{
    NumericValue result;
    result.literal = value;
    return result;
}

NumericValue numeric_expression_value(
    std::string source,
    const Bindings& global_bindings,
    double fallback)
{
    NumericValue result;
    result.literal = fallback;
    result.expression = numeric_expression(std::move(source), global_bindings);
    return result;
}

NumericEvaluation evaluate_numeric(
    const NumericValue& value,
    const InstructionExecutionFrame& frame,
    const Engine* engine)
{
    if (!value.expression) return {value.literal, std::nullopt};
    const auto& evaluator = engine ? *engine : default_engine();
    auto evaluated = evaluate_expression(
        evaluator,
        *value.expression,
        locals_from_frame(frame),
        frame.effective_seed.value_or(frame.context.global_seed));
    if (!evaluated.success()) return {value.literal, format_diagnostics(evaluated)};
    const auto number = scalar_to_double(*evaluated.value);
    if (!number.has_value() || !std::isfinite(*number)) {
        return {value.literal, "Numeric expression must evaluate to a finite number."};
    }
    return {*number, std::nullopt};
}

NumericEvaluation evaluate_numeric_range(
    const NumericRange& range,
    const InstructionExecutionFrame& frame,
    const Engine* engine)
{
    auto minimum = evaluate_numeric(range.minimum, frame, engine);
    if (minimum.error) return minimum;
    if (!range.maximum) return minimum;
    auto maximum = evaluate_numeric(*range.maximum, frame, engine);
    if (maximum.error) return maximum;
    if (maximum.value < minimum.value) return minimum;

    auto step = range.step ? evaluate_numeric(*range.step, frame, engine) : NumericEvaluation{-1.0, std::nullopt};
    if (step.error) return step;
    if (step.value > 0.0) {
        const auto slots = static_cast<std::int64_t>(
            std::floor((maximum.value - minimum.value) / step.value));
        if (slots <= 0) return minimum;
        std::mt19937_64 generator(frame.effective_seed.value_or(frame.context.global_seed));
        std::uniform_int_distribution<std::int64_t> distribution(0, slots);
        return {minimum.value + static_cast<double>(distribution(generator)) * step.value, std::nullopt};
    }

    std::mt19937_64 generator(frame.effective_seed.value_or(frame.context.global_seed));
    std::uniform_real_distribution<double> distribution(minimum.value, maximum.value);
    return {distribution(generator), std::nullopt};
}

std::string numeric_value_revision(const NumericValue& value, const Engine& engine)
{
    std::ostringstream stream;
    stream << value.literal;
    if (value.expression) stream << ":expr:" << expression_configuration_revision(*value.expression, engine);
    return stream.str();
}

std::string numeric_range_revision(const NumericRange& range, const Engine& engine)
{
    auto result = numeric_value_revision(range.minimum, engine);
    if (range.maximum) result += ":max:" + numeric_value_revision(*range.maximum, engine);
    if (range.step) result += ":step:" + numeric_value_revision(*range.step, engine);
    return result;
}

} // namespace phoenix::scripting
