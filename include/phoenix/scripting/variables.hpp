#pragma once

#include "phoenix/scripting/expression.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace phoenix::scripting {

struct VariableSpec {
    std::string name;
    Value initial_value = 0.0;
    std::optional<ExpressionSpec> update;
};

struct VariablePlan {
    std::vector<VariableSpec> variables;
    Bindings parent_bindings;
    std::shared_ptr<const Engine> engine;
};

struct VariableEvaluation {
    std::optional<Bindings> values;
    std::string error;

    [[nodiscard]] bool success() const noexcept { return values.has_value(); }
};

[[nodiscard]] VariableEvaluation initialize_variables(const VariablePlan& plan);
[[nodiscard]] VariableEvaluation update_variables(const VariablePlan& plan,
    const Bindings& previous, std::size_t next_index, SeedValue seed);
[[nodiscard]] std::string variable_configuration_revision(const VariablePlan& plan);

} // namespace phoenix::scripting
