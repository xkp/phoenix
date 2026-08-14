#pragma once

#include "phoenix/scripting/contract.hpp"

#include <string>

namespace phoenix::scripting {

struct ExpressionSpec {
    Program program{"phoenix-js-expression", 1, {}};
    Bindings global_bindings;
    Limits limits;
};

[[nodiscard]] EvaluationResult evaluate_expression(const Engine& engine,
    const ExpressionSpec& expression, Bindings local_bindings = {},
    SeedValue deterministic_seed = 0,
    const CancellationToken* cancellation = nullptr);

[[nodiscard]] std::string expression_configuration_revision(
    const ExpressionSpec& expression, const Engine& engine);

[[nodiscard]] std::string format_diagnostics(const EvaluationResult& result);

} // namespace phoenix::scripting
