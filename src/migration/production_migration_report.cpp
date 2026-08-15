#include "phoenix/migration/production_migration_report.hpp"

#include <algorithm>

namespace phoenix::migration {
namespace {

void append_discovery_diagnostics(
    std::vector<MigrationDiagnostic>& target,
    const ProductionProjectDiscovery& discovery)
{
    for (const auto& diagnostic : discovery.diagnostics) {
        target.push_back(MigrationDiagnostic{
            MigrationDiagnosticSeverity::error,
            "discovery." + to_string(diagnostic.code),
            diagnostic.message,
            diagnostic.path,
            diagnostic.function_id,
            {}});
    }
}

void append_raw_load_diagnostics(
    std::vector<MigrationDiagnostic>& target,
    const RawProductionProjectSet& raw)
{
    for (const auto& diagnostic : raw.diagnostics) {
        target.push_back(MigrationDiagnostic{
            MigrationDiagnosticSeverity::error,
            "raw_load." + to_string(diagnostic.code),
            diagnostic.message,
            diagnostic.path,
            diagnostic.function_id,
            {}});
    }
}

void append_function_link_diagnostics(
    std::vector<MigrationDiagnostic>& target,
    const ProductionFunctionLinkResult& linked)
{
    for (const auto& diagnostic : linked.diagnostics) {
        target.push_back(MigrationDiagnostic{
            MigrationDiagnosticSeverity::error,
            "function_link." + to_string(diagnostic.code),
            diagnostic.message,
            diagnostic.path,
            diagnostic.function_id,
            {}});
    }
}

void append_label_diagnostics(
    std::vector<MigrationDiagnostic>& target,
    const ProductionLabelRegistryBuild& labels)
{
    for (const auto& diagnostic : labels.linked_labels.diagnostics) {
        target.push_back(MigrationDiagnostic{
            MigrationDiagnosticSeverity::error,
            "labels." + to_string(diagnostic.code),
            diagnostic.message,
            {},
            diagnostic.function_id.value_or(FunctionId{}),
            diagnostic.uid.value_or(LabelUid{})});
    }
}

void append_profile_diagnostics(
    std::vector<MigrationDiagnostic>& target,
    const ProductionProfileRegistryBuild& profiles)
{
    for (const auto& diagnostic : profiles.diagnostics) {
        target.push_back(MigrationDiagnostic{
            MigrationDiagnosticSeverity::error,
            "profiles." + to_string(diagnostic.code),
            diagnostic.message,
            diagnostic.path,
            diagnostic.function_id,
            {}});
    }
}

void append_graph_diagnostics(
    std::vector<MigrationDiagnostic>& target,
    const ProductionGraphAdaptResult& graphs)
{
    for (const auto& diagnostic : graphs.diagnostics) {
        target.push_back(MigrationDiagnostic{
            MigrationDiagnosticSeverity::error,
            "graph." + to_string(diagnostic.code),
            diagnostic.message,
            {},
            diagnostic.function_id,
            {}});
    }
}

} // namespace

bool ProductionMigrationReport::ok() const noexcept
{
    return error_count() == 0;
}

std::size_t ProductionMigrationReport::error_count() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        diagnostics.begin(),
        diagnostics.end(),
        [](const auto& diagnostic) {
            return diagnostic.severity == MigrationDiagnosticSeverity::error;
        }));
}

std::size_t ProductionMigrationReport::warning_count() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        diagnostics.begin(),
        diagnostics.end(),
        [](const auto& diagnostic) {
            return diagnostic.severity == MigrationDiagnosticSeverity::warning;
        }));
}

ProductionMigrationReport ProductionMigrationReporter::build_report(
    const std::vector<std::filesystem::path>& roots) const
{
    return build_report(ProductionProjectRawLoader{}.load(roots));
}

ProductionMigrationReport ProductionMigrationReporter::build_report(
    const std::vector<std::filesystem::path>& roots,
    const ProductionMigrationOverrides& overrides) const
{
    auto raw = ProductionProjectRawLoader{}.load(roots);
    auto applied = ProductionMigrationOverrideApplier{}.apply(std::move(raw), overrides);
    return build_report(std::move(applied.raw), std::move(applied.applied));
}

ProductionMigrationReport ProductionMigrationReporter::build_report(
    RawProductionProjectSet raw,
    std::vector<AppliedMigrationOverride> applied_overrides) const
{
    ProductionMigrationReport report;
    report.discovery = raw.discovery;
    report.raw = std::move(raw);
    report.applied_overrides = std::move(applied_overrides);
    report.linked_functions = ProductionFunctionLinker{}.link(report.raw);
    report.labels = ProductionLabelRegistryBuilder{}.build(report.raw);
    report.profiles = ProductionProfileRegistryBuilder{}.build(report.raw, report.linked_functions);
    report.graphs = ProductionGraphAdapter{}.adapt(report.linked_functions);

    append_discovery_diagnostics(report.diagnostics, report.discovery);
    append_raw_load_diagnostics(report.diagnostics, report.raw);
    append_function_link_diagnostics(report.diagnostics, report.linked_functions);
    append_label_diagnostics(report.diagnostics, report.labels);
    append_profile_diagnostics(report.diagnostics, report.profiles);
    append_graph_diagnostics(report.diagnostics, report.graphs);
    return report;
}

std::string to_string(MigrationDiagnosticSeverity severity)
{
    switch (severity) {
    case MigrationDiagnosticSeverity::info: return "info";
    case MigrationDiagnosticSeverity::warning: return "warning";
    case MigrationDiagnosticSeverity::error: return "error";
    }
    return "unknown";
}

} // namespace phoenix::migration
