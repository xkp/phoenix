#include "phoenix/migration/production_repair_plan.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_repair_plan_tests";
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

bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

bool test_repair_plan_lists_missing_function_and_label_choices()
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
        R"({"nodes":[{"id":1,"typeName":"function","inputs":[],"outputs":[],"data":{"file":"CHILD@22222222-2222-2222-2222-222222222222"}},{"id":2,"typeName":"function","inputs":[],"outputs":[],"data":{"file":"MISSING@33333333-3333-3333-3333-333333333333"}}],"links":[]})");
    write_text(child_directory / child_id,
        R"({"name":"CHILD","id":"CHILD@22222222-2222-2222-2222-222222222222","sceneId":"CHILD@22222222-2222-2222-2222-222222222222.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL_CHANGED","visible":true,"color":"#111111"}]})");
    write_text(child_directory / (child_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionMigrationReporter reporter;
    const auto report = reporter.build_report({root / "projects"});
    const auto plan = phoenix::migration::ProductionRepairPlanBuilder{}.build(report);
    const auto json = phoenix::migration::ProductionRepairPlanJsonWriter{}.write(plan);

    return !report.ok()
        && plan.items.size() == 2
        && contains(json, "function:MISSING@33333333-3333-3333-3333-333333333333")
        && contains(json, "ignore_unresolved_function_calls")
        && contains(json, "install_function_and_rerun")
        && contains(json, "label:AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA")
        && contains(json, "WALL")
        && contains(json, "WALL_CHANGED");
}

} // namespace

int main()
{
    if (!test_repair_plan_lists_missing_function_and_label_choices()) {
        std::cerr << "production repair plan tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
