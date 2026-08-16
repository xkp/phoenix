#pragma once

#include "phoenix/graph.hpp"
#include "phoenix/migration/production_migration_report.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace phoenix::migration {

enum class PackageEmissionDiagnosticCode {
    migration_report_has_errors,
    retired_instruction_method,
};

struct PackageEmissionDiagnostic {
    PackageEmissionDiagnosticCode code;
    std::string message;
};

struct MigratedInstructionPayload {
    std::string id;
    std::filesystem::path source_path;
    std::string text;
};

struct MigratedFunctionPackage {
    FunctionDescriptor graph;
    std::filesystem::path manifest_path;
    std::filesystem::path nodes_path;
    std::vector<std::filesystem::path> origins;
    std::map<std::string, MigratedInstructionPayload> payloads;
    std::map<NodeId, std::string> instruction_node_data;
    std::map<std::string, double> numeric_variables;
    std::map<std::string, std::string> profile_texts;
    std::uint64_t fingerprint = 0;
};

struct MigratedProjectPackage {
    std::string schema_version = "p12.migrated-project.v0";
    FunctionId root_function_id;
    std::uint64_t label_registry_fingerprint = 0;
    std::map<LabelUid, std::int32_t> label_ids;
    std::map<FunctionId, MigratedFunctionPackage> functions;
};

struct PackageEmissionResult {
    MigratedProjectPackage package;
    std::vector<PackageEmissionDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class MigratedProjectPackageBuilder {
public:
    [[nodiscard]] PackageEmissionResult build(
        const ProductionMigrationReport& report) const;
};

[[nodiscard]] std::string to_string(PackageEmissionDiagnosticCode code);

} // namespace phoenix::migration
