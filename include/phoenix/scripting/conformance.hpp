#pragma once

#include "phoenix/scripting/contract.hpp"

namespace phoenix::scripting {

enum class ConformanceFeature {
    literal,
    arithmetic,
    comparison,
    boolean_logic,
    conditional,
    string,
    binding,
    local_shadowing,
    invalid_source,
    unsupported_result,
    isolation,
    budget,
};

struct ConformanceCase {
    std::string id;
    ConformanceFeature feature = ConformanceFeature::literal;
    Program program;
    Bindings global_bindings;
    Bindings local_bindings;
    EvaluationStatus expected_status = EvaluationStatus::completed;
    std::optional<Value> expected_value;
    std::optional<DiagnosticCode> expected_diagnostic;
};

struct LegacyMigrationResult {
    bool success = false;
    std::string source;
    Bindings bindings;
    std::map<std::string, std::string> aliases;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] const std::vector<ConformanceCase>& version_one_conformance_corpus();
[[nodiscard]] LegacyMigrationResult migrate_legacy_bracket_bindings(
    const std::string& source, const Bindings& legacy_bindings);

} // namespace phoenix::scripting
