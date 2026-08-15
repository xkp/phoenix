#include "phoenix/migration/production_graph_adapter.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_graph_adapter_tests";
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

phoenix::migration::ProductionFunctionLinkResult link_fixture(const std::filesystem::path& root)
{
    const phoenix::migration::ProductionProjectRawLoader loader;
    const auto raw = loader.load({root / "projects"});
    const phoenix::migration::ProductionFunctionLinker linker;
    return linker.link(raw);
}

bool test_adapts_nodes_ports_function_calls_payloads_and_links()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto tool_id = std::string{"TOOL@22222222-2222-2222-2222-222222222222"};
    const auto payload_id = std::string{"33333333-3333-3333-3333-333333333333"};
    const auto project_directory = root / "projects" / project_id;
    const auto tool_directory = root / "functions" / tool_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"id":1,"typeName":"functionInput","inputs":[],"outputs":[{"name":"input","dataType":"geometry"}]},{"id":2,"typeName":"partition","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}],"data":{"file":"33333333-3333-3333-3333-333333333333"}},{"id":3,"typeName":"function","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"OUT","dataType":"geometry"}],"data":{"file":"TOOL@22222222-2222-2222-2222-222222222222"}},{"id":4,"typeName":"functionOutput","inputs":[{"name":"output","dataType":"geometry"}],"outputs":[]}],"links":[{"outputNode":0,"outputSocket":0,"inputNode":1,"inputSocket":0},{"outputNode":1,"outputSocket":0,"inputNode":2,"inputSocket":0},{"outputNode":2,"outputSocket":0,"inputNode":3,"inputSocket":0}]})");
    write_text(project_directory / payload_id, R"({"conditions":[]})");
    write_text(tool_directory / tool_id,
        R"({"name":"TOOL","id":"TOOL@22222222-2222-2222-2222-222222222222","sceneId":"TOOL@22222222-2222-2222-2222-222222222222.nodes"})");
    write_text(tool_directory / (tool_id + ".nodes"), R"({"nodes":[]})");

    const auto linked = link_fixture(root);
    const phoenix::migration::ProductionGraphAdapter adapter;
    const auto adapted = adapter.adapt(linked);
    const auto& function = adapted.functions.at(project_id);

    return adapted.ok()
        && function.input_ports.size() == 1
        && function.output_ports.size() == 1
        && function.instructions.size() == 2
        && function.instructions[0].kind == "partition"
        && function.instructions[0].configuration_revision == "payload:" + payload_id
        && function.instructions[1].called_function_id
        && *function.instructions[1].called_function_id == tool_id
        && function.edges.size() == 3;
}

bool test_uses_order_index_links_even_when_tokens_match_node_ids()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto project_directory = root / "projects" / project_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"id":0,"typeName":"functionInput","inputs":[],"outputs":[{"name":"input","dataType":"geometry"}]},{"id":1,"typeName":"functionOutput","inputs":[{"name":"output","dataType":"geometry"}],"outputs":[]},{"id":2,"typeName":"select","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"A","dataType":"geometry"},{"name":"B","dataType":"geometry"}]},{"id":3,"typeName":"rename","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}]}],"links":[{"outputNode":0,"outputSocket":0,"inputNode":2,"inputSocket":0},{"outputNode":2,"outputSocket":1,"inputNode":3,"inputSocket":0},{"outputNode":3,"outputSocket":0,"inputNode":1,"inputSocket":0}]})");

    const auto linked = link_fixture(root);
    const phoenix::migration::ProductionGraphAdapter adapter;
    const auto adapted = adapter.adapt(linked);
    const auto& function = adapted.functions.at(project_id);

    return adapted.ok()
        && function.edges.size() == 3
        && function.edges[1].from_node == 2
        && function.edges[1].to_node == 3;
}

bool test_reports_unresolved_link_socket()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto project_directory = root / "projects" / project_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"id":10,"typeName":"extrusion","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}]},{"id":20,"typeName":"rename","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}]}],"links":[{"outputNode":0,"outputSocket":2,"inputNode":1,"inputSocket":0}]})");

    const auto linked = link_fixture(root);
    const phoenix::migration::ProductionGraphAdapter adapter;
    const auto adapted = adapter.adapt(linked);

    return !adapted.ok()
        && adapted.diagnostics.front().code
            == phoenix::migration::GraphAdaptDiagnosticCode::unresolved_link_socket;
}

bool test_disabled_nodes_and_disabled_only_branches_are_pruned()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto project_directory = root / "projects" / project_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"id":1,"typeName":"functionInput","inputs":[],"outputs":[{"name":"input","dataType":"geometry"}]},{"id":2,"typeName":"rename","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}]},{"id":3,"typeName":"functionOutput","inputs":[{"name":"output","dataType":"geometry"}],"outputs":[]},{"id":4,"typeName":"rename","disabled":true,"inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}]},{"id":5,"typeName":"extrusion","inputs":[{"name":"input","dataType":"geometry"}],"outputs":[{"name":"output","dataType":"geometry"}]}],"links":[{"outputNode":0,"outputSocket":0,"inputNode":1,"inputSocket":0},{"outputNode":1,"outputSocket":0,"inputNode":2,"inputSocket":0},{"outputNode":0,"outputSocket":0,"inputNode":3,"inputSocket":0},{"outputNode":3,"outputSocket":4,"inputNode":4,"inputSocket":0}]})");

    const auto linked = link_fixture(root);
    const phoenix::migration::ProductionGraphAdapter adapter;
    const auto adapted = adapter.adapt(linked);
    const auto& function = adapted.functions.at(project_id);

    return adapted.ok()
        && function.instructions.size() == 1
        && function.instructions.front().id == 2
        && function.edges.size() == 2;
}

} // namespace

int main()
{
    const bool ok = test_adapts_nodes_ports_function_calls_payloads_and_links()
        && test_uses_order_index_links_even_when_tokens_match_node_ids()
        && test_reports_unresolved_link_socket()
        && test_disabled_nodes_and_disabled_only_branches_are_pruned();
    if (!ok) {
        std::cerr << "production graph adapter tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
