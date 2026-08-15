#include "phoenix/migration/production_migrated_package.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_overrides_tests";
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

bool test_label_remap_repairs_unknown_label_reference()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto directory = root / "projects" / project_id;

    write_text(directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"select","data":{"labelOutputs":["BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB"]}}],"links":[]})");

    const phoenix::migration::ProductionMigrationReporter reporter;
    const auto broken = reporter.build_report({root / "projects"});
    phoenix::migration::ProductionMigrationOverrides overrides;
    overrides.label_uid_remaps.push_back({
        "BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB",
        "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA"});
    const auto repaired = reporter.build_report({root / "projects"}, overrides);

    return !broken.ok()
        && repaired.ok()
        && repaired.applied_overrides.size() == 1
        && repaired.applied_overrides.front().kind == "label_uid_remap";
}

bool test_function_reference_rewrite_repairs_stale_import()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto stale_id = std::string{"OLD_TOOL@22222222-2222-2222-2222-222222222222"};
    const auto tool_id = std::string{"TOOL@33333333-3333-3333-3333-333333333333"};
    const auto project_directory = root / "projects" / project_id;
    const auto tool_directory = root / "functions" / tool_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"id":1,"typeName":"function","inputs":[],"outputs":[],"data":{"file":"OLD_TOOL@22222222-2222-2222-2222-222222222222"}}],"links":[]})");
    write_text(tool_directory / tool_id,
        R"({"name":"TOOL","id":"TOOL@33333333-3333-3333-3333-333333333333","sceneId":"TOOL@33333333-3333-3333-3333-333333333333.nodes"})");
    write_text(tool_directory / (tool_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionMigrationReporter reporter;
    const auto broken = reporter.build_report({root / "projects"});
    phoenix::migration::ProductionMigrationOverrides overrides;
    overrides.function_reference_rewrites.push_back({stale_id, tool_id});
    const auto repaired = reporter.build_report({root / "projects"}, overrides);
    const phoenix::migration::MigratedProjectPackageBuilder package_builder;
    const auto emitted = package_builder.build(repaired);

    return !broken.ok()
        && repaired.ok()
        && repaired.linked_functions.functions.count(tool_id) == 1
        && repaired.linked_functions.call_counts.at(project_id).at(tool_id) == 1
        && repaired.applied_overrides.size() == 1
        && repaired.applied_overrides.front().kind == "function_reference_rewrite"
        && emitted.ok();
}

} // namespace

int main()
{
    const bool ok = test_label_remap_repairs_unknown_label_reference()
        && test_function_reference_rewrite_repairs_stale_import();
    if (!ok) {
        std::cerr << "production migration overrides tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
