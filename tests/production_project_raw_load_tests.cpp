#include "phoenix/migration/production_project_raw_load.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_raw_load_tests";
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
    const phoenix::migration::RawProductionProjectSet& loaded,
    phoenix::migration::RawLoadDiagnosticCode code,
    const std::string& function_id)
{
    for (const auto& diagnostic : loaded.diagnostics) {
        if (diagnostic.code == code && diagnostic.function_id == function_id) return true;
    }
    return false;
}

bool test_preserves_manifest_nodes_and_payload_text()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto payload_id = std::string{"22222222-2222-2222-2222-222222222222"};
    const auto directory = root / "projects" / project_id;
    const auto manifest_text =
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})";
    const auto nodes_text =
        R"({"nodes":[{"typeName":"partition","data":{"file":"22222222-2222-2222-2222-222222222222"}}]})";
    const auto payload_text = R"({"conditions":[{"typeId":"parallel"}]})";

    write_text(directory / project_id, manifest_text);
    write_text(directory / (project_id + ".nodes"), nodes_text);
    write_text(directory / payload_id, payload_text);

    const phoenix::migration::ProductionProjectRawLoader loader;
    const auto loaded = loader.load({root / "projects"});
    const auto& function = loaded.functions.at(project_id).front();

    return loaded.projects.size() == 1
        && function.manifest_text == manifest_text
        && function.nodes_text == nodes_text
        && function.payload_blobs.at(payload_id).text == payload_text
        && loaded.diagnostics.empty();
}

bool test_reports_missing_payload_blob()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto directory = root / "projects" / project_id;

    write_text(directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"partition","data":{"file":"33333333-3333-3333-3333-333333333333"}}]})");

    const phoenix::migration::ProductionProjectRawLoader loader;
    const auto loaded = loader.load({root / "projects"});

    return loaded.functions.at(project_id).front().payload_blobs.empty()
        && has_diagnostic(
            loaded,
            phoenix::migration::RawLoadDiagnosticCode::missing_payload_blob,
            project_id);
}

bool test_does_not_load_function_references_as_payloads()
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
    const auto loaded = loader.load({root / "projects"});

    return loaded.functions.at(project_id).front().payload_blobs.empty()
        && loaded.functions.count(tool_id) == 1
        && loaded.diagnostics.empty();
}

} // namespace

int main()
{
    const bool ok = test_preserves_manifest_nodes_and_payload_text()
        && test_reports_missing_payload_blob()
        && test_does_not_load_function_references_as_payloads();
    if (!ok) {
        std::cerr << "production project raw load tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
