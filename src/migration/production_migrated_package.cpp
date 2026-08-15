#include "phoenix/migration/production_migrated_package.hpp"

#include <algorithm>

namespace phoenix::migration {

PackageEmissionResult MigratedProjectPackageBuilder::build(
    const ProductionMigrationReport& report) const
{
    PackageEmissionResult result;
    if (!report.ok()) {
        result.diagnostics.push_back(PackageEmissionDiagnostic{
            PackageEmissionDiagnosticCode::migration_report_has_errors,
            "Cannot emit migrated project package while migration report has errors."});
        return result;
    }

    if (!report.discovery.projects.empty()) {
        result.package.root_function_id = report.discovery.projects.front().id;
    } else if (!report.graphs.functions.empty()) {
        result.package.root_function_id = report.graphs.functions.begin()->first;
    }
    result.package.label_registry_fingerprint =
        report.labels.linked_labels.registry.semantic_fingerprint();

    for (const auto& entry : report.linked_functions.functions) {
        const auto graph_it = report.graphs.functions.find(entry.first);
        if (graph_it == report.graphs.functions.end()) continue;

        MigratedFunctionPackage function;
        function.graph = graph_it->second;
        function.manifest_path = entry.second.function.candidate.manifest_path;
        function.nodes_path = entry.second.function.candidate.nodes_path;
        function.origins.assign(entry.second.origins.begin(), entry.second.origins.end());
        std::sort(function.origins.begin(), function.origins.end());
        function.fingerprint = entry.second.fingerprint;

        for (const auto& payload_entry : entry.second.function.payload_blobs) {
            function.payloads.emplace(payload_entry.first, MigratedInstructionPayload{
                payload_entry.second.id,
                payload_entry.second.path,
                payload_entry.second.text});
        }

        result.package.functions.emplace(entry.first, std::move(function));
    }

    return result;
}

std::string to_string(PackageEmissionDiagnosticCode code)
{
    switch (code) {
    case PackageEmissionDiagnosticCode::migration_report_has_errors:
        return "migration_report_has_errors";
    }
    return "unknown";
}

} // namespace phoenix::migration
