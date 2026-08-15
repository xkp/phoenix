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
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(directory / (project_id + ".nodes"),
        R"({"nodes":[{"id":1,"typeName":"partition","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}],"data":{"file":"22222222-2222-2222-2222-222222222222"}}],"links":[]})");
    write_text(directory / payload_id, R"({"conditions":[]})");

    const phoenix::migration::ProductionMigrationReporter reporter;
    const auto report = reporter.build_report({root / "projects"});
    const phoenix::migration::MigratedProjectPackageBuilder builder;
    const auto emitted = builder.build(report);

    return report.ok()
        && emitted.ok()
        && emitted.package.schema_version == "p12.migrated-project.v0"
        && emitted.package.root_function_id == project_id
        && emitted.package.label_registry_fingerprint != 0
        && emitted.package.functions.count(project_id) == 1
        && emitted.package.functions.at(project_id).graph.instructions.size() == 1
        && emitted.package.functions.at(project_id).payloads.at(payload_id).text == R"({"conditions":[]})";
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

} // namespace

int main()
{
    const bool ok = test_emits_minimal_package_from_clean_report()
        && test_refuses_package_when_report_has_errors();
    if (!ok) {
        std::cerr << "production migrated package tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
