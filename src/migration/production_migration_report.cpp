#include "phoenix/migration/production_migration_report.hpp"

#include <algorithm>
#include <optional>
#include <regex>

namespace phoenix::migration {
namespace {

std::optional<NodeId> extract_node_id_value(const std::string& text)
{
    const std::regex pattern{R"regex("id"\s*:\s*([0-9]+))regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    if (it == std::sregex_iterator{}) return std::nullopt;
    try {
        return static_cast<NodeId>(std::stoull((*it)[1].str()));
    } catch (...) {
        return std::nullopt;
    }
}

std::string extract_string_value(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*"([^"]*)")regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    return it == std::sregex_iterator{} ? std::string{} : (*it)[1].str();
}

bool extract_bool_value(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*(true|false))regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    return it != std::sregex_iterator{} && (*it)[1].str() == "true";
}

std::string extract_object_field(const std::string& text, const std::string& key)
{
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return {};
    const auto object_start = text.find('{', key_pos);
    if (object_start == std::string::npos) return {};

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = object_start; index < text.size(); ++index) {
        const auto ch = text[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (ch == '{') ++depth;
        if (ch == '}') {
            --depth;
            if (depth == 0) return text.substr(object_start, index - object_start + 1);
        }
    }
    return {};
}

std::string extract_array_text(const std::string& text, const std::string& key)
{
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return {};
    const auto array_start = text.find('[', key_pos);
    if (array_start == std::string::npos) return {};

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = array_start; index < text.size(); ++index) {
        const auto ch = text[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (ch == '[') ++depth;
        if (ch == ']') {
            --depth;
            if (depth == 0) return text.substr(array_start + 1, index - array_start - 1);
        }
    }
    return {};
}

std::vector<std::string> extract_object_texts(const std::string& text)
{
    std::vector<std::string> objects;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t object_start = std::string::npos;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto ch = text[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (ch == '{') {
            if (depth == 0) object_start = index;
            ++depth;
        }
        if (ch == '}') {
            --depth;
            if (depth == 0 && object_start != std::string::npos) {
                objects.push_back(text.substr(object_start, index - object_start + 1));
                object_start = std::string::npos;
            }
        }
    }
    return objects;
}

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

void append_instruction_diagnostics(
    std::vector<MigrationDiagnostic>& target,
    const RawProductionProjectSet& raw)
{
    for (const auto& entry : raw.functions) {
        for (const auto& function : entry.second) {
            const auto nodes_text = extract_array_text(function.nodes_text, "nodes");
            for (const auto& node : extract_object_texts(nodes_text)) {
                if (extract_bool_value(node, "disabled")) continue;
                if (extract_string_value(node, "typeName") != "extrusion") continue;
                const auto data = extract_object_field(node, "data");
                if (extract_string_value(data, "method") != "label") continue;
                const auto node_id = extract_node_id_value(node);
                if (!node_id.has_value()) continue;
                target.push_back(MigrationDiagnostic{
                    MigrationDiagnosticSeverity::error,
                    "instructions.retired_label_method",
                    "Extrusion method 'label' is retired. Ignore this instruction for this migration or edit the production source and rerun.",
                    function.candidate.nodes_path,
                    function.candidate.id,
                    {},
                    *node_id});
            }
        }
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
    append_instruction_diagnostics(report.diagnostics, report.raw);
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
