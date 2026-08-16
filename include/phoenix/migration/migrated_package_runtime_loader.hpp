#pragma once

#include "phoenix/migration/production_migrated_package.hpp"

#include <string>
#include <vector>

namespace phoenix::migration {

enum class MigratedPackageLoadDiagnosticCode {
    missing_root_function,
    graph_validation_failed,
};

struct MigratedPackageLoadDiagnostic {
    MigratedPackageLoadDiagnosticCode code;
    FunctionId function_id;
    std::string message;
};

struct LoadedMigratedPackage {
    FunctionId root_function_id;
    std::map<LabelUid, std::int32_t> label_ids;
    std::map<FunctionId, MigratedFunctionPackage> functions;
};

struct MigratedPackageLoadResult {
    LoadedMigratedPackage package;
    std::vector<MigratedPackageLoadDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class MigratedPackageRuntimeLoader {
public:
    [[nodiscard]] MigratedPackageLoadResult load(const MigratedProjectPackage& package) const;
};

[[nodiscard]] std::string to_string(MigratedPackageLoadDiagnosticCode code);

} // namespace phoenix::migration
