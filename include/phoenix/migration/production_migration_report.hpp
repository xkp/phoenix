#pragma once

#include "phoenix/migration/production_graph_adapter.hpp"
#include "phoenix/migration/production_label_registry.hpp"
#include "phoenix/migration/production_migration_overrides.hpp"
#include "phoenix/migration/production_profile_registry.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace phoenix::migration {

enum class MigrationDiagnosticSeverity {
    info,
    warning,
    error,
};

struct MigrationDiagnostic {
    MigrationDiagnosticSeverity severity = MigrationDiagnosticSeverity::error;
    std::string code;
    std::string message;
    std::filesystem::path path;
    FunctionId function_id;
    LabelUid label_uid;
};

struct ProductionMigrationReport {
    ProductionProjectDiscovery discovery;
    RawProductionProjectSet raw;
    ProductionFunctionLinkResult linked_functions;
    ProductionLabelRegistryBuild labels;
    ProductionProfileRegistryBuild profiles;
    ProductionGraphAdaptResult graphs;
    std::vector<AppliedMigrationOverride> applied_overrides;
    std::vector<MigrationDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] std::size_t error_count() const noexcept;
    [[nodiscard]] std::size_t warning_count() const noexcept;
};

class ProductionMigrationReporter {
public:
    [[nodiscard]] ProductionMigrationReport build_report(
        const std::vector<std::filesystem::path>& roots) const;
    [[nodiscard]] ProductionMigrationReport build_report(
        const std::vector<std::filesystem::path>& roots,
        const ProductionMigrationOverrides& overrides) const;
    [[nodiscard]] ProductionMigrationReport build_report(
        RawProductionProjectSet raw,
        std::vector<AppliedMigrationOverride> applied_overrides = {}) const;
};

[[nodiscard]] std::string to_string(MigrationDiagnosticSeverity severity);

} // namespace phoenix::migration
