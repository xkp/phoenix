#include "phoenix/migration/production_migration_report.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_migration_report_tests";
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

bool has_code(
    const phoenix::migration::ProductionMigrationReport& report,
    const std::string& code)
{
    for (const auto& diagnostic : report.diagnostics) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

bool test_successful_report_contains_pipeline_outputs()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto directory = root / "projects" / project_id;

    write_text(directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(directory / (project_id + ".nodes"),
        R"({"nodes":[{"id":1,"typeName":"functionInput","inputs":[],"outputs":[{"name":"input","dataType":"geometry"}]}],"links":[]})");

    const phoenix::migration::ProductionMigrationReporter reporter;
    const auto report = reporter.build_report({root / "projects"});

    return report.ok()
        && report.error_count() == 0
        && report.discovery.projects.size() == 1
        && report.raw.functions.count(project_id) == 1
        && report.linked_functions.functions.count(project_id) == 1
        && report.labels.linked_labels.registry.size() == 1
        && report.graphs.functions.count(project_id) == 1;
}

bool test_report_collects_cross_phase_errors()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto child_id = std::string{"CHILD@22222222-2222-2222-2222-222222222222"};
    const auto missing_id = std::string{"MISSING@33333333-3333-3333-3333-333333333333"};
    const auto project_directory = root / "projects" / project_id;
    const auto child_directory = root / "functions" / child_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"id":1,"typeName":"partition","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}],"data":{"file":"44444444-4444-4444-4444-444444444444"}},{"id":2,"typeName":"function","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}],"data":{"file":"CHILD@22222222-2222-2222-2222-222222222222"}},{"id":3,"typeName":"function","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}],"data":{"file":"MISSING@33333333-3333-3333-3333-333333333333"}}],"links":[{"outputNode":1,"outputSocket":4,"inputNode":2,"inputSocket":0}]})");
    write_text(child_directory / child_id,
        R"({"name":"CHILD","id":"CHILD@22222222-2222-2222-2222-222222222222","sceneId":"CHILD@22222222-2222-2222-2222-222222222222.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL_CHANGED","visible":true,"color":"#111111"}]})");
    write_text(child_directory / (child_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionMigrationReporter reporter;
    const auto report = reporter.build_report({root / "projects"});

    return !report.ok()
        && report.error_count() >= 4
        && has_code(report, "raw_load.missing_payload_blob")
        && has_code(report, "function_link.unresolved_function_reference")
        && has_code(report, "labels.conflicting_definition")
        && has_code(report, "graph.unresolved_link_socket");
}

} // namespace

int main()
{
    const bool ok = test_successful_report_contains_pipeline_outputs()
        && test_report_collects_cross_phase_errors();
    if (!ok) {
        std::cerr << "production migration report tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
