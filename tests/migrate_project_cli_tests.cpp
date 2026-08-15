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

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: migrate_project_cli_tests <phoenix_migrate_project_exe>\n";
        return EXIT_FAILURE;
    }

    const auto exe = std::filesystem::path{argv[1]};
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
        return EXIT_FAILURE;
    }
    if (!std::filesystem::exists(output)
        || !file_contains(output, "PHOENIX_MIGRATED_PACKAGE_TEXT_V0")
        || !file_contains(output, project_id)) {
        std::cerr << "migration package output was not written as expected\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
