#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_migrate_project_cli_tests";
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

bool file_contains(const std::filesystem::path& path, const std::string& needle)
{
    std::ifstream input(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return text.find(needle) != std::string::npos;
}

bool test_successful_migration_writes_package(const std::filesystem::path& exe)
{
    const auto root = temp_root();
    const auto output = root / "out.phxmig";
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto directory = root / "projects" / project_id;

    write_text(directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(directory / (project_id + ".nodes"), R"({"nodes":[],"links":[]})");

    const auto command = "\"\"" + exe.string() + "\" \"" + (root / "projects").string()
        + "\" \"" + output.string() + "\"\"";
    const auto exit_code = std::system(command.c_str());
    if (exit_code != 0) {
        std::cerr << "phoenix_migrate_project returned " << exit_code << "\n";
        return false;
    }
    if (!std::filesystem::exists(output)
        || !file_contains(output, "PHOENIX_MIGRATED_PACKAGE_TEXT_V0")
        || !file_contains(output, project_id)) {
        std::cerr << "migration package output was not written as expected\n";
        return false;
    }
    return true;
}

bool test_interactive_repair_writes_selection_file(const std::filesystem::path& exe)
{
    const auto root = temp_root();
    const auto output = root / "out.phxmig";
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto child_id = std::string{"CHILD@22222222-2222-2222-2222-222222222222"};
    const auto project_directory = root / "projects" / project_id;
    const auto child_directory = root / "functions" / child_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"CHILD@22222222-2222-2222-2222-222222222222"}}],"links":[]})");
    write_text(child_directory / child_id,
        R"({"name":"CHILD","id":"CHILD@22222222-2222-2222-2222-222222222222","sceneId":"CHILD@22222222-2222-2222-2222-222222222222.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL_CHANGED","visible":true,"color":"#111111"}]})");
    write_text(child_directory / (child_id + ".nodes"), R"({"nodes":[]})");

    const auto transcript = root / "interactive.txt";
    const auto command = "cmd /s /c \"echo 1| \"\"" + exe.string()
        + "\"\" --interactive-repair \"\"" + (root / "projects").string()
        + "\"\" \"\"" + output.string() + "\"\" > \"\"" + transcript.string()
        + "\"\" 2>&1\"";
    const auto exit_code = std::system(command.c_str());
    const auto selection = output.string() + ".repair.selection.json";

    return exit_code == 0
        && std::filesystem::exists(selection)
        && file_contains(selection, "label:AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA")
        && file_contains(selection, "choice_1")
        && std::filesystem::exists(output)
        && file_contains(output, "PHOENIX_MIGRATED_PACKAGE_TEXT_V0");
}

bool test_saved_repair_selection_writes_package(const std::filesystem::path& exe)
{
    const auto root = temp_root();
    const auto output = root / "out.phxmig";
    const auto selection = root / "selection.json";
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto child_id = std::string{"CHILD@22222222-2222-2222-2222-222222222222"};
    const auto project_directory = root / "projects" / project_id;
    const auto child_directory = root / "functions" / child_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"CHILD@22222222-2222-2222-2222-222222222222"}}],"links":[]})");
    write_text(child_directory / child_id,
        R"({"name":"CHILD","id":"CHILD@22222222-2222-2222-2222-222222222222","sceneId":"CHILD@22222222-2222-2222-2222-222222222222.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL_CHANGED","visible":true,"color":"#111111"}]})");
    write_text(child_directory / (child_id + ".nodes"), R"({"nodes":[]})");
    write_text(selection,
        R"({"schema":"PHOENIX_PRODUCTION_REPAIR_SELECTION_V0","selections":[{"repair_id":"label:AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","choice_id":"choice_1"}]})");

    const auto command = "\"\"" + exe.string() + "\" --repair-selection \"" + selection.string()
        + "\" \"" + (root / "projects").string() + "\" \"" + output.string() + "\"\"";
    const auto exit_code = std::system(command.c_str());

    return exit_code == 0
        && std::filesystem::exists(output)
        && file_contains(output, "PHOENIX_MIGRATED_PACKAGE_TEXT_V0");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: migrate_project_cli_tests <phoenix_migrate_project_exe>\n";
        return EXIT_FAILURE;
    }

    const auto exe = std::filesystem::path{argv[1]};
    if (!test_successful_migration_writes_package(exe)
        || !test_interactive_repair_writes_selection_file(exe)
        || !test_saved_repair_selection_writes_package(exe)) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
