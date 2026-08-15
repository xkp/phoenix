#include "phoenix/migration/production_function_linker.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_function_linker_tests";
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

bool has_diagnostic(
    const phoenix::migration::ProductionFunctionLinkResult& result,
    phoenix::migration::FunctionLinkDiagnosticCode code,
    const std::string& function_id)
{
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code && diagnostic.function_id == function_id) return true;
    }
    return false;
}

bool test_links_reachable_project_and_function_library_definition()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto tool_id = std::string{"TOOL@22222222-2222-2222-2222-222222222222"};
    const auto project_directory = root / "projects" / project_id;
    const auto tool_directory = root / "functions" / tool_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"TOOL@22222222-2222-2222-2222-222222222222"}}]})");
    write_text(tool_directory / tool_id,
        R"({"name":"TOOL","id":"TOOL@22222222-2222-2222-2222-222222222222","sceneId":"TOOL@22222222-2222-2222-2222-222222222222.nodes"})");
    write_text(tool_directory / (tool_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionProjectRawLoader loader;
    const auto raw = loader.load({root / "projects"});
    const phoenix::migration::ProductionFunctionLinker linker;
    const auto result = linker.link(raw);

    return result.ok()
        && result.reachable_function_ids.count(project_id) == 1
        && result.reachable_function_ids.count(tool_id) == 1
        && result.functions.count(project_id) == 1
        && result.functions.count(tool_id) == 1;
}

bool test_repeated_call_sites_do_not_duplicate_function_body()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto tool_id = std::string{"TOOL@22222222-2222-2222-2222-222222222222"};
    const auto project_directory = root / "projects" / project_id;
    const auto tool_directory = root / "functions" / tool_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"TOOL@22222222-2222-2222-2222-222222222222"}},{"typeName":"function","data":{"file":"TOOL@22222222-2222-2222-2222-222222222222"}}]})");
    write_text(tool_directory / tool_id,
        R"({"name":"TOOL","id":"TOOL@22222222-2222-2222-2222-222222222222","sceneId":"TOOL@22222222-2222-2222-2222-222222222222.nodes"})");
    write_text(tool_directory / (tool_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionProjectRawLoader loader;
    const auto raw = loader.load({root / "projects"});
    const phoenix::migration::ProductionFunctionLinker linker;
    const auto result = linker.link(raw);

    return result.ok()
        && result.functions.count(tool_id) == 1
        && result.call_counts.at(project_id).at(tool_id) == 2
        && result.references.size() == 1
        && result.references.front().caller_id == project_id
        && result.references.front().callee_id == tool_id
        && result.references.front().call_count == 2;
}

bool test_deduplicates_identical_duplicate_candidates()
{
    const auto raw_id = std::string{"TOOL@22222222-2222-2222-2222-222222222222"};
    phoenix::migration::RawProductionProjectSet raw;
    phoenix::migration::RawProductionFunction first;
    first.candidate.id = raw_id;
    first.candidate.manifest_path = "first/TOOL";
    first.manifest_text = "same manifest";
    first.nodes_text = "same nodes";
    phoenix::migration::RawProductionFunction second = first;
    second.candidate.manifest_path = "second/TOOL";
    raw.functions[raw_id] = {first, second};

    const phoenix::migration::ProductionFunctionLinker linker;
    const auto result = linker.link(raw);

    return result.ok()
        && result.functions.count(raw_id) == 1
        && result.functions.at(raw_id).origins.size() == 2;
}

bool test_deduplicates_published_self_contained_copy()
{
    const auto raw_id = std::string{"TOOL@22222222-2222-2222-2222-222222222222"};
    phoenix::migration::RawProductionProjectSet raw;
    phoenix::migration::RawProductionFunction project;
    project.candidate.id = raw_id;
    project.candidate.manifest_path = "projects/TOOL";
    project.manifest_text = R"({"name":"TOOL","id":"TOOL@22222222-2222-2222-2222-222222222222","sceneId":"TOOL.nodes"})";
    project.nodes_text = R"({"nodes":[],"links":[]})";
    phoenix::migration::RawProductionFunction published = project;
    published.candidate.manifest_path = "functions/TOOL";
    published.manifest_text =
        "{\n"
        "    \"name\": \"TOOL\",\n"
        "    \"id\": \"TOOL@22222222-2222-2222-2222-222222222222\",\n"
        "    \"sceneId\": \"TOOL.nodes\",\n"
        "    \"selfContained\": true\n"
        "}";
    raw.functions[raw_id] = {project, published};

    const phoenix::migration::ProductionFunctionLinker linker;
    const auto result = linker.link(raw);

    return result.ok()
        && result.functions.count(raw_id) == 1
        && result.functions.at(raw_id).origins.size() == 2;
}

bool test_reports_conflicting_duplicate_candidates()
{
    const auto raw_id = std::string{"TOOL@22222222-2222-2222-2222-222222222222"};
    phoenix::migration::RawProductionProjectSet raw;
    phoenix::migration::RawProductionFunction first;
    first.candidate.id = raw_id;
    first.candidate.manifest_path = "first/TOOL";
    first.manifest_text = "first manifest";
    first.nodes_text = "nodes";
    phoenix::migration::RawProductionFunction second = first;
    second.candidate.manifest_path = "second/TOOL";
    second.manifest_text = "second manifest";
    raw.functions[raw_id] = {first, second};

    const phoenix::migration::ProductionFunctionLinker linker;
    const auto result = linker.link(raw);

    return !result.ok()
        && result.functions.count(raw_id) == 1
        && has_diagnostic(
            result,
            phoenix::migration::FunctionLinkDiagnosticCode::conflicting_function_definition,
            raw_id);
}

bool test_reports_unresolved_function_reference()
{
    const auto root_id = std::string{"ROOT@11111111-1111-1111-1111-111111111111"};
    const auto missing_id = std::string{"MISSING@22222222-2222-2222-2222-222222222222"};
    phoenix::migration::RawProductionProjectSet raw;
    phoenix::migration::RawProductionProject project;
    project.candidate.id = root_id;
    raw.projects.push_back(project);
    phoenix::migration::RawProductionFunction root;
    root.candidate.id = root_id;
    root.candidate.referenced_function_ids.insert(missing_id);
    root.candidate.manifest_path = "root/ROOT";
    root.manifest_text = "root manifest";
    root.nodes_text = "root nodes";
    raw.functions[root_id].push_back(root);

    const phoenix::migration::ProductionFunctionLinker linker;
    const auto result = linker.link(raw);

    return !result.ok()
        && result.unresolved_function_ids.count(missing_id) == 1
        && has_diagnostic(
            result,
            phoenix::migration::FunctionLinkDiagnosticCode::unresolved_function_reference,
            missing_id);
}

} // namespace

int main()
{
    const bool ok = test_links_reachable_project_and_function_library_definition()
        && test_repeated_call_sites_do_not_duplicate_function_body()
        && test_deduplicates_identical_duplicate_candidates()
        && test_deduplicates_published_self_contained_copy()
        && test_reports_conflicting_duplicate_candidates()
        && test_reports_unresolved_function_reference();
    if (!ok) {
        std::cerr << "production function linker tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
