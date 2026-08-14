#include "phoenix/scripting/variables.hpp"

#include "phoenix/scripting/quickjs_engine.hpp"

#include <set>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace phoenix::scripting {
namespace {

bool valid_name(const std::string& name)
{
    if (name.empty() || name == "$index" || name == "_index") return false;
    EvaluationRequest request;
    request.program.source = "0";
    request.local_bindings.emplace(name, 0.0);
    return validate_request(request).success();
}

std::shared_ptr<const Engine> engine(const VariablePlan& plan)
{
    return plan.engine ? plan.engine : std::make_shared<QuickJsEngine>();
}

void append_value(std::ostringstream& stream, const Value& value)
{
    stream << std::setprecision(std::numeric_limits<double>::max_digits10);
    stream << value.index() << ':';
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) stream << (item ? "true" : "false");
        else stream << item;
    }, value);
}

} // namespace

VariableEvaluation initialize_variables(const VariablePlan& plan)
{
    Bindings result;
    std::set<std::string> names;
    for (const auto& variable : plan.variables) {
        if (!valid_name(variable.name))
            return {{}, "Invalid or reserved loop variable name: " + variable.name};
        if (!names.insert(variable.name).second)
            return {{}, "Duplicate loop variable name: " + variable.name};
        result.emplace(variable.name, variable.initial_value);
    }
    return {std::move(result), {}};
}

VariableEvaluation update_variables(const VariablePlan& plan,
    const Bindings& previous, std::size_t next_index, SeedValue seed)
{
    auto locals = plan.parent_bindings;
    for (const auto& value : previous) locals[value.first] = value.second;
    locals["_index"] = static_cast<std::int64_t>(next_index);
    Bindings next = previous;
    const auto evaluator = engine(plan);
    for (const auto& variable : plan.variables) {
        if (!variable.update) continue;
        const auto evaluated = evaluate_expression(*evaluator, *variable.update, locals, seed);
        if (!evaluated.success())
            return {{}, "Loop variable '" + variable.name + "': " + format_diagnostics(evaluated)};
        next[variable.name] = *evaluated.value;
    }
    return {std::move(next), {}};
}

std::string variable_configuration_revision(const VariablePlan& plan)
{
    const auto evaluator = engine(plan);
    std::ostringstream stream;
    stream << "variables-v1";
    for (const auto& binding : plan.parent_bindings) {
        stream << '|' << binding.first << ':';
        append_value(stream, binding.second);
    }
    for (const auto& variable : plan.variables) {
        stream << '|' << variable.name << ':';
        append_value(stream, variable.initial_value);
        if (variable.update)
            stream << ':' << expression_configuration_revision(*variable.update, *evaluator);
    }
    return stream.str();
}

} // namespace phoenix::scripting
