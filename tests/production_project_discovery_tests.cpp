#include "phoenix/migration/production_project_discovery.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_discovery_tests";
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

bool test_discovers_project_and_local_function()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto child_id = std::string{"CHILD@22222222-2222-2222-2222-222222222222"};
    const auto directory = root / project_id;

    write_text(directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","functions":[{"id":"CHILD@22222222-2222-2222-2222-222222222222","name":"CHILD","imported":false}]})");
    write_text(directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"CHILD@22222222-2222-2222-2222-222222222222"}}]})");
    write_text(directory / child_id,
        R"({"name":"CHILD","id":"CHILD@22222222-2222-2222-2222-222222222222","sceneId":"CHILD@22222222-2222-2222-2222-222222222222.nodes"})");
    write_text(directory / (child_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionProjectDiscoverer discoverer;
    const auto result = discoverer.discover({root});

    return result.projects.size() == 1
        && result.projects.front().id == project_id
        && result.projects.front().declared_function_ids.count(child_id) == 1
        && result.functions.count(project_id) == 1
        && result.functions.count(child_id) == 1
        && result.imported_function_references.count(child_id) == 1
        && result.unresolved_imported_function_ids.empty();
}

bool test_reports_unresolved_imported_function_reference()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto missing_id = std::string{"TOOL@33333333-3333-3333-3333-333333333333"};
    const auto directory = root / project_id;

    write_text(directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"TOOL@33333333-3333-3333-3333-333333333333","imported":true}}]})");

    const phoenix::migration::ProductionProjectDiscoverer discoverer;
    const auto result = discoverer.discover({root});

    return result.imported_function_references.count(missing_id) == 1
        && result.unresolved_imported_function_ids.count(missing_id) == 1;
}

bool test_reports_missing_nodes_file()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto directory = root / project_id;

    write_text(directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");

    const phoenix::migration::ProductionProjectDiscoverer discoverer;
    const auto result = discoverer.discover({root});

    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == phoenix::migration::DiscoveryDiagnosticCode::missing_nodes_file
            && diagnostic.function_id == project_id) return true;
    }
    return false;
}

bool test_projects_root_includes_sibling_functions_library()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto tool_id = std::string{"TOOL@33333333-3333-3333-3333-333333333333"};
    const auto projects = root / "projects";
    const auto functions = root / "functions";
    const auto project_directory = projects / project_id;
    const auto tool_directory = functions / tool_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"TOOL@33333333-3333-3333-3333-333333333333","imported":true}}]})");
    write_text(tool_directory / tool_id,
        R"({"name":"TOOL","id":"TOOL@33333333-3333-3333-3333-333333333333","sceneId":"TOOL@33333333-3333-3333-3333-333333333333.nodes"})");
    write_text(tool_directory / (tool_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionProjectDiscoverer discoverer;
    const auto result = discoverer.discover({projects});

    return result.projects.size() == 1
        && result.projects.front().id == project_id
        && result.functions.count(tool_id) == 1
        && result.imported_function_references.count(tool_id) == 1
        && result.unresolved_imported_function_ids.empty();
}

bool test_single_project_root_includes_sibling_functions_library()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto tool_id = std::string{"TOOL@33333333-3333-3333-3333-333333333333"};
    const auto projects = root / "projects";
    const auto functions = root / "functions";
    const auto project_directory = projects / project_id;
    const auto tool_directory = functions / tool_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"TOOL@33333333-3333-3333-3333-333333333333","imported":true}}]})");
    write_text(tool_directory / tool_id,
        R"({"name":"TOOL","id":"TOOL@33333333-3333-3333-3333-333333333333","sceneId":"TOOL@33333333-3333-3333-3333-333333333333.nodes"})");
    write_text(tool_directory / (tool_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionProjectDiscoverer discoverer;
    const auto result = discoverer.discover({project_directory});

    return result.projects.size() == 1
        && result.projects.front().id == project_id
        && result.functions.count(tool_id) == 1
        && result.imported_function_references.count(tool_id) == 1
        && result.unresolved_imported_function_ids.empty();
}

bool test_published_project_function_copy_is_not_discovery_error()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto project_directory = root / "projects" / project_id;
    const auto function_directory = root / "functions" / project_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(project_directory / (project_id + ".nodes"), R"({"nodes":[]})");
    write_text(function_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(function_directory / (project_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionProjectDiscoverer discoverer;
    const auto result = discoverer.discover({project_directory});

    return result.projects.size() == 1
        && result.functions.count(project_id) == 1
        && result.functions.at(project_id).size() == 2
        && result.diagnostics.empty();
}

bool test_disabled_function_nodes_do_not_create_function_references()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto disabled_id = std::string{"DISABLED@22222222-2222-2222-2222-222222222222"};
    const auto directory = root / "projects" / project_id;

    write_text(directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(directory / (project_id + ".nodes"),
        R"({"nodes":[{"id":1,"typeName":"function","disabled":true,"data":{"file":"DISABLED@22222222-2222-2222-2222-222222222222"}}]})");

    const phoenix::migration::ProductionProjectDiscoverer discoverer;
    const auto result = discoverer.discover({root / "projects"});

    return result.imported_function_references.count(disabled_id) == 0
        && result.unresolved_imported_function_ids.count(disabled_id) == 0;
}

} // namespace

int main()
{
    const bool ok = test_discovers_project_and_local_function()
        && test_reports_unresolved_imported_function_reference()
        && test_reports_missing_nodes_file()
        && test_projects_root_includes_sibling_functions_library()
        && test_single_project_root_includes_sibling_functions_library()
        && test_published_project_function_copy_is_not_discovery_error()
        && test_disabled_function_nodes_do_not_create_function_references();
    if (!ok) {
        std::cerr << "production project discovery tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
