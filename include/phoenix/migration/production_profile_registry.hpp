#pragma once

#include "phoenix/migration/production_function_linker.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace phoenix::migration {

enum class ProductionProfileDiagnosticCode {
    conflicting_definition,
};

struct ProductionProfileDiagnostic {
    ProductionProfileDiagnosticCode code;
    std::string message;
    FunctionId function_id;
    std::string profile_id;
    std::string profile_name;
    std::filesystem::path path;
};

struct ProductionProfileDefinition {
    std::string id;
    std::string name;
    std::string canonical_text;
    FunctionId function_id;
    std::filesystem::path path;
};

struct ProductionProfileRegistryBuild {
    std::map<std::string, ProductionProfileDefinition> profiles;
    std::map<FunctionId, std::vector<ProductionProfileDefinition>> declarations;
    std::vector<ProductionProfileDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class ProductionProfileRegistryBuilder {
public:
    [[nodiscard]] ProductionProfileRegistryBuild build(
        const RawProductionProjectSet& raw,
        const ProductionFunctionLinkResult& linked) const;
};

[[nodiscard]] std::string to_string(ProductionProfileDiagnosticCode code);

} // namespace phoenix::migration
