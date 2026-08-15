#pragma once

#include "phoenix/migration/production_project_raw_load.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace phoenix::migration {

enum class FunctionLinkDiagnosticCode {
    conflicting_function_definition,
    unresolved_function_reference,
};

struct FunctionLinkDiagnostic {
    FunctionLinkDiagnosticCode code;
    std::string message;
    FunctionId function_id;
    std::filesystem::path path;
};

struct LinkedProductionFunction {
    FunctionId id;
    RawProductionFunction function;
    std::set<std::filesystem::path> origins;
    std::uint64_t fingerprint = 0;
};

struct ProductionFunctionReference {
    FunctionId caller_id;
    FunctionId callee_id;
    std::size_t call_count = 0;
};

struct ProductionFunctionLinkResult {
    std::map<FunctionId, LinkedProductionFunction> functions;
    std::map<FunctionId, std::map<FunctionId, std::size_t>> call_counts;
    std::vector<ProductionFunctionReference> references;
    std::set<FunctionId> reachable_function_ids;
    std::set<FunctionId> unresolved_function_ids;
    std::vector<FunctionLinkDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class ProductionFunctionLinker {
public:
    [[nodiscard]] ProductionFunctionLinkResult link(
        const RawProductionProjectSet& raw) const;
};

[[nodiscard]] std::string to_string(FunctionLinkDiagnosticCode code);

} // namespace phoenix::migration
