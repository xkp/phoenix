#include "phoenix/migration/production_migrated_package.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_minimal_migration_tests";
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

bool test_emits_self_contained_minimal_migrated_package()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto tool_id = std::string{"TOOL@22222222-2222-2222-2222-222222222222"};
    const auto payload_id = std::string{"33333333-3333-3333-3333-333333333333"};
    const auto label_id = std::string{"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA"};
    const auto project_directory = root / "projects" / project_id;
    const auto tool_directory = root / "functions" / tool_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"id":1,"typeName":"functionInput","inputs":[],"outputs":[{"name":"input","dataType":"geometry"}]},{"id":2,"typeName":"partition","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}],"data":{"file":"33333333-3333-3333-3333-333333333333","label":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA"}},{"id":3,"typeName":"function","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}],"data":{"file":"TOOL@22222222-2222-2222-2222-222222222222"}},{"id":4,"typeName":"functionOutput","inputs":[{"name":"output","dataType":"geometry"}],"outputs":[]}],"links":[{"outputNode":0,"outputSocket":0,"inputNode":1,"inputSocket":0},{"outputNode":1,"outputSocket":0,"inputNode":2,"inputSocket":0},{"outputNode":2,"outputSocket":0,"inputNode":3,"inputSocket":0}]})");
    write_text(project_directory / payload_id, R"({"conditions":[{"typeId":"isLabeled","data":{"label":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA"}}]})");

    write_text(tool_directory / tool_id,
        R"({"name":"TOOL","id":"TOOL@22222222-2222-2222-2222-222222222222","sceneId":"TOOL@22222222-2222-2222-2222-222222222222.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(tool_directory / (tool_id + ".nodes"),
        R"({"nodes":[{"id":10,"typeName":"rename","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}],"data":{"all_faces":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA"}}],"links":[]})");

    const phoenix::migration::ProductionMigrationReporter reporter;
    const auto report = reporter.build_report({root / "projects"});
    const phoenix::migration::MigratedProjectPackageBuilder builder;
    const auto emitted = builder.build(report);

    if (!report.ok() || !emitted.ok()) return false;
    const auto& package = emitted.package;
    const auto& root_function = package.functions.at(project_id);
    const auto& tool_function = package.functions.at(tool_id);

    return package.root_function_id == project_id
        && package.functions.size() == 2
        && package.label_registry_fingerprint != 0
        && report.labels.linked_labels.registry.size() == 1
        && report.labels.linked_labels.registry.find_uid(label_id)
        && report.linked_functions.call_counts.at(project_id).at(tool_id) == 1
        && root_function.graph.instructions.size() == 2
        && root_function.graph.edges.size() == 3
        && root_function.payloads.at(payload_id).text.find("isLabeled") != std::string::npos
        && tool_function.graph.instructions.size() == 1
        && tool_function.graph.instructions.front().kind == "rename";
}

} // namespace

int main()
{
    const bool ok = test_emits_self_contained_minimal_migrated_package();
    if (!ok) {
        std::cerr << "production minimal migration tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
