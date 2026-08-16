#include "phoenix/migration/production_migrated_package.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_migrated_package_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void write_text(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
}

bool test_emits_minimal_package_from_clean_report()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto payload_id = std::string{"22222222-2222-2222-2222-222222222222"};
    const auto directory = root / "projects" / project_id;

    write_text(directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}],"profiles":[{"id":"33333333-3333-3333-3333-333333333333","name":"MAIN","visible":true}],"variables":[{"name":"HEIGHT","value":{"value":4.5},"isExpression":false},{"name":"DOUBLE_HEIGHT","value":{"value":0},"isExpression":true,"expression":"HEIGHT*2"},{"name":"SKIP","value":{"value":9},"isExpression":true,"expression":"MISSING+1"}]})");
    write_text(directory / (project_id + ".nodes"),
        R"({"nodes":[{"id":1,"typeName":"partition","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}],"data":{"file":"22222222-2222-2222-2222-222222222222"}}],"links":[]})");
    write_text(directory / payload_id, R"({"conditions":[]})");
    write_text(directory / "33333333-3333-3333-3333-333333333333",
        R"({"closed":false,"segments":[[{"x":0,"y":0},{"x":1,"y":2},{}]],"data":{}})");

    const phoenix::migration::ProductionMigrationReporter reporter;
    const auto report = reporter.build_report({root / "projects"});
    const phoenix::migration::MigratedProjectPackageBuilder builder;
    const auto emitted = builder.build(report);

    return report.ok()
        && emitted.ok()
        && emitted.package.schema_version == "p12.migrated-project.v0"
        && emitted.package.root_function_id == project_id
        && emitted.package.label_registry_fingerprint != 0
        && emitted.package.label_ids.count("AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA") == 1
        && emitted.package.functions.count(project_id) == 1
        && emitted.package.functions.at(project_id).graph.instructions.size() == 1
        && emitted.package.functions.at(project_id).payloads.at(payload_id).text == R"({"conditions":[]})"
        && emitted.package.functions.at(project_id).instruction_node_data.at(1)
            == R"({"file":"22222222-2222-2222-2222-222222222222"})"
        && emitted.package.functions.at(project_id).numeric_variables.at("HEIGHT") == 4.5
        && emitted.package.functions.at(project_id).numeric_variables.at("DOUBLE_HEIGHT") == 9.0
        && emitted.package.functions.at(project_id).numeric_variables.count("SKIP") == 0
        && emitted.package.functions.at(project_id).profile_texts.count(
            "33333333-3333-3333-3333-333333333333") == 1;
}

bool test_refuses_package_when_report_has_errors()
{
    phoenix::migration::ProductionMigrationReport report;
    report.diagnostics.push_back(phoenix::migration::MigrationDiagnostic{
        phoenix::migration::MigrationDiagnosticSeverity::error,
        "test.error",
        "Synthetic error.",
        {},
        {},
        {}});

    const phoenix::migration::MigratedProjectPackageBuilder builder;
    const auto emitted = builder.build(report);

    return !emitted.ok()
        && emitted.diagnostics.size() == 1
        && emitted.diagnostics.front().code
            == phoenix::migration::PackageEmissionDiagnosticCode::migration_report_has_errors
        && emitted.package.functions.empty();
}

bool test_emits_call_site_variable_overrides()
{
    const auto root = temp_root();
    const auto root_id = std::string{"ROOT@11111111-1111-1111-1111-111111111111"};
    const auto tool_id = std::string{"TOOL@22222222-2222-2222-2222-222222222222"};
    const auto directory = root / "projects" / root_id;

    write_text(directory / root_id,
        R"({"name":"ROOT","id":"ROOT@11111111-1111-1111-1111-111111111111","sceneId":"ROOT@11111111-1111-1111-1111-111111111111.nodes","functions":[{"id":"TOOL@22222222-2222-2222-2222-222222222222","name":"TOOL","imported":true}],"variables":[]})");
    write_text(directory / (root_id + ".nodes"),
        R"({"nodes":[{"id":1,"typeName":"function","data":{"file":"TOOL@22222222-2222-2222-2222-222222222222","variables":[{"name":"HEIGHT","value":{"value":7},"isExpression":false},{"name":"DOUBLE_HEIGHT","value":{"value":0},"isExpression":true,"expression":"HEIGHT*2"}]}}],"links":[]})");
    write_text(directory / tool_id,
        R"({"name":"TOOL","id":"TOOL@22222222-2222-2222-2222-222222222222","sceneId":"TOOL@22222222-2222-2222-2222-222222222222.nodes","variables":[]})");
    write_text(directory / (tool_id + ".nodes"),
        R"({"nodes":[],"links":[]})");

    const phoenix::migration::ProductionMigrationReporter reporter;
    const auto report = reporter.build_report({root / "projects"});
    const phoenix::migration::MigratedProjectPackageBuilder builder;
    const auto emitted = builder.build(report);

    const auto& variables = emitted.package.functions.at(tool_id).numeric_variables;
    return report.ok()
        && emitted.ok()
        && variables.at("HEIGHT") == 7.0
        && variables.at("DOUBLE_HEIGHT") == 14.0;
}

bool test_refuses_retired_label_extrusion()
{
    const auto root = temp_root();
    const auto project_id = std::string{"ROOT@11111111-1111-1111-1111-111111111111"};
    const auto directory = root / "projects" / project_id;

    write_text(directory / project_id,
        R"({"name":"ROOT","id":"ROOT@11111111-1111-1111-1111-111111111111","sceneId":"ROOT@11111111-1111-1111-1111-111111111111.nodes","variables":[]})");
    write_text(directory / (project_id + ".nodes"),
        R"({"nodes":[{"id":7,"typeName":"extrusion","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}],"data":{"method":"label"}}],"links":[]})");

    const phoenix::migration::ProductionMigrationReporter reporter;
    const auto report = reporter.build_report({root / "projects"});
    const phoenix::migration::MigratedProjectPackageBuilder builder;
    const auto emitted = builder.build(report);

    return report.ok()
        && !emitted.ok()
        && emitted.diagnostics.front().code
            == phoenix::migration::PackageEmissionDiagnosticCode::retired_instruction_method;
}

} // namespace

int main()
{
    const bool ok = test_emits_minimal_package_from_clean_report()
        && test_emits_call_site_variable_overrides()
        && test_refuses_retired_label_extrusion()
        && test_refuses_package_when_report_has_errors();
    if (!ok) {
        std::cerr << "production migrated package tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
