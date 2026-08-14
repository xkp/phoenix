#include "phoenix/scripting/expression.hpp"

#include <iomanip>
#include <sstream>

namespace phoenix::scripting {

EvaluationResult evaluate_expression(const Engine& engine,
    const ExpressionSpec& expression, Bindings local_bindings,
    SeedValue deterministic_seed, const CancellationToken* cancellation)
{
    EvaluationRequest request;
    request.program = expression.program;
    request.global_bindings = expression.global_bindings;
    request.local_bindings = std::move(local_bindings);
    request.limits = expression.limits;
    request.deterministic_seed = deterministic_seed;
    return engine.evaluate(request, cancellation);
}

std::string expression_configuration_revision(
    const ExpressionSpec& expression, const Engine& engine)
{
    EvaluationRequest request;
    request.program = expression.program;
    request.global_bindings = expression.global_bindings;
    request.limits = expression.limits;
    std::ostringstream stream;
    stream << "expression-v1:" << std::hex << std::setfill('0') << std::setw(16)
           << cache_fingerprint(request, engine.engine_id(), engine.engine_version());
    return stream.str();
}

std::string format_diagnostics(const EvaluationResult& result)
{
    std::ostringstream stream;
    for (std::size_t i = 0; i < result.diagnostics.size(); ++i) {
        if (i) stream << "; ";
        stream << to_string(result.diagnostics[i].code) << ": "
               << result.diagnostics[i].message;
    }
    return stream.str().empty() ? "Expression evaluation failed." : stream.str();
}

} // namespace phoenix::scripting
