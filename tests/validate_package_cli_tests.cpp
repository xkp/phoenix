#include "phoenix/migration/production_migrated_package_io.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p13_validate_package_cli_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

bool file_contains(const std::filesystem::path& path, const std::string& needle)
{
    std::ifstream input(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return text.find(needle) != std::string::npos;
}

bool test_valid_package_passes(const std::filesystem::path& exe)
{
    const auto root = temp_root();
    const auto package_path = root / "package.phxmig";
    const auto transcript = root / "out.txt";

    phoenix::migration::MigratedProjectPackage package;
    package.root_function_id = "ROOT@11111111-1111-1111-1111-111111111111";
    phoenix::migration::MigratedFunctionPackage function;
    function.graph.id = package.root_function_id;
    package.functions.emplace(package.root_function_id, function);

    const phoenix::migration::MigratedProjectPackageWriter writer;
    if (!writer.write(package, package_path).empty()) return false;

    const auto command = "\"\"" + exe.string() + "\" \"" + package_path.string()
        + "\" > \"" + transcript.string() + "\"\"";
    const auto exit_code = std::system(command.c_str());
    return exit_code == 0
        && file_contains(transcript, "graph_validation: ok")
        && file_contains(transcript, "validation: ok");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: validate_package_cli_tests <phoenix_validate_package_exe>\n";
        return EXIT_FAILURE;
    }

    if (!test_valid_package_passes(argv[1])) {
        std::cerr << "validate package CLI tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
