#include "phoenix/scripting/conformance.hpp"

#include <cctype>
#include <set>

namespace phoenix::scripting {
namespace {

std::string alias_for(const std::string& name, std::size_t ordinal)
{
    std::string alias = "legacy_";
    for (const auto character : name) {
        const auto value = static_cast<unsigned char>(character);
        alias += std::isalnum(value) || character == '_' ? character : '_';
    }
    alias += "_" + std::to_string(ordinal);
    return alias;
}

ConformanceCase value_case(std::string id, ConformanceFeature feature,
    std::string source, Value expected, Bindings globals = {}, Bindings locals = {})
{
    return {std::move(id), feature, {"phoenix-js-expression", 1, std::move(source)},
        std::move(globals), std::move(locals), EvaluationStatus::completed,
        std::move(expected), {}};
}

} // namespace

const std::vector<ConformanceCase>& version_one_conformance_corpus()
{
    static const std::vector<ConformanceCase> cases = {
        value_case("literal.integer", ConformanceFeature::literal, "42", std::int64_t{42}),
        value_case("literal.double", ConformanceFeature::literal, "2.5", 2.5),
        value_case("literal.boolean", ConformanceFeature::literal, "true", true),
        value_case("literal.string", ConformanceFeature::string, "'wall'", std::string{"wall"}),
        value_case("arithmetic.precedence", ConformanceFeature::arithmetic, "2 + 3 * 4", std::int64_t{14}),
        value_case("arithmetic.binding", ConformanceFeature::binding, "height * width", 12.0,
            {{"height", 3.0}, {"width", 4.0}}),
        value_case("comparison.numeric", ConformanceFeature::comparison, "height >= 3", true,
            {{"height", 3.0}}),
        value_case("logic.short-circuit", ConformanceFeature::boolean_logic,
            "enabled && height > 2", false, {{"enabled", false}, {"height", 5.0}}),
        value_case("conditional.ternary", ConformanceFeature::conditional,
            "enabled ? height : 0", 5.0, {{"enabled", true}, {"height", 5.0}}),
        value_case("scope.local-shadow", ConformanceFeature::local_shadowing,
            "height", 7.0, {{"height", 2.0}}, {{"height", 7.0}}),
        {"error.syntax", ConformanceFeature::invalid_source,
            {"phoenix-js-expression", 1, "1 +"}, {}, {}, EvaluationStatus::failed,
            {}, DiagnosticCode::compile_error},
        {"result.object", ConformanceFeature::unsupported_result,
            {"phoenix-js-expression", 1, "({ value: 1 })"}, {}, {}, EvaluationStatus::rejected,
            {}, DiagnosticCode::unsupported_result},
        {"isolation.globalThis", ConformanceFeature::isolation,
            {"phoenix-js-expression", 1, "globalThis.process"}, {}, {}, EvaluationStatus::failed,
            {}, DiagnosticCode::evaluation_error},
        {"budget.infinite-loop", ConformanceFeature::budget,
            {"phoenix-js-expression", 1, "(() => { while (true) {} })()"}, {}, {},
            EvaluationStatus::budget_exceeded, {}, DiagnosticCode::instruction_budget_exceeded},
    };
    return cases;
}

LegacyMigrationResult migrate_legacy_bracket_bindings(
    const std::string& source, const Bindings& legacy_bindings)
{
    LegacyMigrationResult result;
    std::set<std::string> used_aliases;
    std::size_t cursor = 0;
    while (cursor < source.size()) {
        if (source[cursor] != '[') {
            result.source += source[cursor++];
            continue;
        }
        const auto close = source.find(']', cursor + 1);
        if (close == std::string::npos) {
            result.diagnostics.push_back({DiagnosticCode::compile_error,
                "Legacy binding has no closing bracket.", {}, {}});
            return result;
        }
        const auto name = source.substr(cursor + 1, close - cursor - 1);
        const auto found = legacy_bindings.find(name);
        if (name.empty() || found == legacy_bindings.end()) {
            result.diagnostics.push_back({DiagnosticCode::invalid_binding_name,
                "Unknown legacy binding: " + name, {}, {}});
            return result;
        }
        auto alias_it = result.aliases.find(name);
        if (alias_it == result.aliases.end()) {
            auto alias = alias_for(name, result.aliases.size());
            while (!used_aliases.insert(alias).second) alias += "_";
            alias_it = result.aliases.emplace(name, std::move(alias)).first;
            result.bindings.emplace(alias_it->second, found->second);
        }
        result.source += alias_it->second;
        cursor = close + 1;
    }
    result.success = true;
    return result;
}

} // namespace phoenix::scripting
