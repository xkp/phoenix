#include "phoenix/migration/production_profile_registry.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_profile_registry_tests";
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

phoenix::migration::ProductionProfileRegistryBuild build_profiles(
    const std::filesystem::path& projects_root)
{
    const phoenix::migration::ProductionProjectRawLoader loader;
    const auto raw = loader.load({projects_root});
    const auto linked = phoenix::migration::ProductionFunctionLinker{}.link(raw);
    return phoenix::migration::ProductionProfileRegistryBuilder{}.build(raw, linked);
}

bool has_profile_diagnostic(
    const phoenix::migration::ProductionProfileRegistryBuild& build,
    phoenix::migration::ProductionProfileDiagnosticCode code,
    const std::string& id)
{
    for (const auto& diagnostic : build.diagnostics) {
        if (diagnostic.code == code && diagnostic.profile_id == id) return true;
    }
    return false;
}

bool test_deduplicates_identical_profiles_across_reachable_functions()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto child_id = std::string{"CHILD@22222222-2222-2222-2222-222222222222"};
    const auto project_directory = root / "projects" / project_id;
    const auto child_directory = root / "functions" / child_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","profiles":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"MAIN","imported":false,"visible":true}]})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"CHILD@22222222-2222-2222-2222-222222222222"}}]})");
    write_text(child_directory / child_id,
        R"({"name":"CHILD","id":"CHILD@22222222-2222-2222-2222-222222222222","sceneId":"CHILD@22222222-2222-2222-2222-222222222222.nodes","profiles":[
            { "id" : "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA", "name" : "MAIN", "imported" : false, "visible" : true }
        ]})");
    write_text(child_directory / (child_id + ".nodes"), R"({"nodes":[]})");

    const auto build = build_profiles(root / "projects");

    return build.ok()
        && build.profiles.size() == 1
        && build.declarations.at(project_id).size() == 1
        && build.declarations.at(child_id).size() == 1;
}

bool test_conflicting_duplicate_profile_id_fails()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto child_id = std::string{"CHILD@22222222-2222-2222-2222-222222222222"};
    const auto project_directory = root / "projects" / project_id;
    const auto child_directory = root / "functions" / child_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","profiles":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"MAIN","imported":false,"visible":true}]})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"CHILD@22222222-2222-2222-2222-222222222222"}}]})");
    write_text(child_directory / child_id,
        R"({"name":"CHILD","id":"CHILD@22222222-2222-2222-2222-222222222222","sceneId":"CHILD@22222222-2222-2222-2222-222222222222.nodes","profiles":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"CHANGED","imported":false,"visible":true}]})");
    write_text(child_directory / (child_id + ".nodes"), R"({"nodes":[]})");

    const auto build = build_profiles(root / "projects");

    return !build.ok()
        && has_profile_diagnostic(
            build,
            phoenix::migration::ProductionProfileDiagnosticCode::conflicting_definition,
            "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA");
}

bool test_ignores_unreachable_profile_conflicts()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto unused_id = std::string{"UNUSED@33333333-3333-3333-3333-333333333333"};
    const auto project_directory = root / "projects" / project_id;
    const auto unused_directory = root / "functions" / unused_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","profiles":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"MAIN","imported":false,"visible":true}]})");
    write_text(project_directory / (project_id + ".nodes"), R"({"nodes":[]})");
    write_text(unused_directory / unused_id,
        R"({"name":"UNUSED","id":"UNUSED@33333333-3333-3333-3333-333333333333","sceneId":"UNUSED@33333333-3333-3333-3333-333333333333.nodes","profiles":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"CHANGED","imported":false,"visible":true}]})");
    write_text(unused_directory / (unused_id + ".nodes"), R"({"nodes":[]})");

    const auto build = build_profiles(root / "projects");

    return build.ok()
        && build.profiles.size() == 1;
}

} // namespace

int main()
{
    const bool ok = test_deduplicates_identical_profiles_across_reachable_functions()
        && test_conflicting_duplicate_profile_id_fails()
        && test_ignores_unreachable_profile_conflicts();
    if (!ok) {
        std::cerr << "production profile registry tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
