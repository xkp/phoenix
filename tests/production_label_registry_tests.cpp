#include "phoenix/migration/production_label_registry.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_label_registry_tests";
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

bool has_label_diagnostic(
    const phoenix::migration::ProductionLabelRegistryBuild& build,
    phoenix::LabelDiagnosticCode code,
    const std::string& uid)
{
    for (const auto& diagnostic : build.linked_labels.diagnostics) {
        if (diagnostic.code == code && diagnostic.uid && *diagnostic.uid == uid) return true;
    }
    return false;
}

bool test_builds_registry_and_maps_visible_to_hidden()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto directory = root / "projects" / project_id;

    write_text(directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"VISIBLE","visible":true,"color":"#111111"},{"id":"BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB","name":"HIDDEN","visible":false,"color":"#222222"}]})");
    write_text(directory / (project_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionProjectRawLoader loader;
    const auto raw = loader.load({root / "projects"});
    const phoenix::migration::ProductionLabelRegistryBuilder builder;
    const auto build = builder.build(raw);

    const auto visible_id = build.linked_labels.registry.find_uid("AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA");
    const auto hidden_id = build.linked_labels.registry.find_uid("BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB");
    if (!build.ok() || !visible_id || !hidden_id) return false;
    const auto* visible = build.linked_labels.registry.find_definition(*visible_id);
    const auto* hidden = build.linked_labels.registry.find_definition(*hidden_id);
    return visible != nullptr && hidden != nullptr
        && !visible->hidden
        && hidden->hidden;
}

bool test_deduplicates_identical_labels_across_reachable_functions()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto child_id = std::string{"CHILD@22222222-2222-2222-2222-222222222222"};
    const auto project_directory = root / "projects" / project_id;
    const auto child_directory = root / "functions" / child_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"CHILD@22222222-2222-2222-2222-222222222222"}}]})");
    write_text(child_directory / child_id,
        R"({"name":"CHILD","id":"CHILD@22222222-2222-2222-2222-222222222222","sceneId":"CHILD@22222222-2222-2222-2222-222222222222.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(child_directory / (child_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionProjectRawLoader loader;
    const auto raw = loader.load({root / "projects"});
    const phoenix::migration::ProductionLabelRegistryBuilder builder;
    const auto build = builder.build(raw);

    return build.ok()
        && build.linked_labels.registry.size() == 1
        && build.declarations.at(project_id).size() == 1
        && build.declarations.at(child_id).size() == 1;
}

bool test_conflicting_duplicate_label_uid_fails()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto child_id = std::string{"CHILD@22222222-2222-2222-2222-222222222222"};
    const auto project_directory = root / "projects" / project_id;
    const auto child_directory = root / "functions" / child_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"CHILD@22222222-2222-2222-2222-222222222222"}}]})");
    write_text(child_directory / child_id,
        R"({"name":"CHILD","id":"CHILD@22222222-2222-2222-2222-222222222222","sceneId":"CHILD@22222222-2222-2222-2222-222222222222.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL_CHANGED","visible":true,"color":"#111111"}]})");
    write_text(child_directory / (child_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionProjectRawLoader loader;
    const auto raw = loader.load({root / "projects"});
    const phoenix::migration::ProductionLabelRegistryBuilder builder;
    const auto build = builder.build(raw);

    return !build.ok()
        && has_label_diagnostic(
            build,
            phoenix::LabelDiagnosticCode::conflicting_definition,
            "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA");
}

bool test_duplicate_label_uid_with_different_color_is_allowed()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto child_id = std::string{"CHILD@22222222-2222-2222-2222-222222222222"};
    const auto project_directory = root / "projects" / project_id;
    const auto child_directory = root / "functions" / child_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"CHILD@22222222-2222-2222-2222-222222222222"}}]})");
    write_text(child_directory / child_id,
        R"({"name":"CHILD","id":"CHILD@22222222-2222-2222-2222-222222222222","sceneId":"CHILD@22222222-2222-2222-2222-222222222222.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#999999"}]})");
    write_text(child_directory / (child_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionProjectRawLoader loader;
    const auto raw = loader.load({root / "projects"});
    const phoenix::migration::ProductionLabelRegistryBuilder builder;
    const auto build = builder.build(raw);

    return build.ok()
        && build.linked_labels.registry.size() == 1
        && build.linked_labels.registry.find_uid("AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA");
}

bool test_declared_node_label_reference_is_allowed()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto payload_id = std::string{"BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB"};
    const auto directory = root / "projects" / project_id;

    write_text(directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"partition","data":{"file":"BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB"}},{"typeName":"select","data":{"labelOutputs":["AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA"]}}]})");
    write_text(directory / payload_id, R"({"conditions":[]})");

    const phoenix::migration::ProductionProjectRawLoader loader;
    const auto raw = loader.load({root / "projects"});
    const phoenix::migration::ProductionLabelRegistryBuilder builder;
    const auto build = builder.build(raw);

    return build.ok()
        && build.linked_labels.registry.find_uid("AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA");
}

bool test_unknown_node_label_reference_fails()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto directory = root / "projects" / project_id;

    write_text(directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"select","data":{"labelOutputs":["CCCCCCCC-CCCC-CCCC-CCCC-CCCCCCCCCCCC"]}}]})");

    const phoenix::migration::ProductionProjectRawLoader loader;
    const auto raw = loader.load({root / "projects"});
    const phoenix::migration::ProductionLabelRegistryBuilder builder;
    const auto build = builder.build(raw);

    return !build.ok()
        && has_label_diagnostic(
            build,
            phoenix::LabelDiagnosticCode::unresolved_reference,
            "CCCCCCCC-CCCC-CCCC-CCCC-CCCCCCCCCCCC");
}

} // namespace

int main()
{
    const bool ok = test_builds_registry_and_maps_visible_to_hidden()
        && test_deduplicates_identical_labels_across_reachable_functions()
        && test_conflicting_duplicate_label_uid_fails()
        && test_duplicate_label_uid_with_different_color_is_allowed()
        && test_declared_node_label_reference_is_allowed()
        && test_unknown_node_label_reference_fails();
    if (!ok) {
        std::cerr << "production label registry tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
