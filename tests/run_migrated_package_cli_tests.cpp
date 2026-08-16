#include "phoenix/migration/production_migrated_package_io.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    const auto root = std::filesystem::temp_directory_path() / "phoenix_p13_run_migrated_package_cli_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

phoenix::PortDescriptor output_port(const phoenix::PortId& id, const phoenix::TypeId& type)
{
    return phoenix::PortDescriptor{id, type, phoenix::PortDirection::output};
}

bool file_contains(const std::filesystem::path& path, const std::string& needle)
{
    std::ifstream input(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return text.find(needle) != std::string::npos;
}

bool test_missing_handler_is_reported(const std::filesystem::path& exe)
{
    const auto root = temp_root();
    const auto package_path = root / "package.phxmig";
    const auto transcript = root / "err.txt";

    phoenix::migration::MigratedProjectPackage package;
    package.root_function_id = "ROOT@44444444-4444-4444-4444-444444444444";

    phoenix::migration::MigratedFunctionPackage function;
    function.graph.id = package.root_function_id;
    phoenix::InstructionDescriptor instruction;
    instruction.id = 10;
    instruction.kind = "not_registered_yet";
    instruction.output_ports.push_back(output_port("output", "geometry"));
    instruction.has_else_port = false;
    function.graph.instructions.push_back(instruction);
    package.functions.emplace(package.root_function_id, function);

    const phoenix::migration::MigratedProjectPackageWriter writer;
    if (!writer.write(package, package_path).empty()) return false;

    const auto command = "\"\"" + exe.string() + "\" \"" + package_path.string()
        + "\" 2> \"" + transcript.string() + "\"\"";
    const auto exit_code = std::system(command.c_str());
    return exit_code != 0
        && file_contains(transcript, "error runtime.unsupported_handler kind=not_registered_yet")
        && file_contains(transcript, "runtime_attempt: blocked");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: run_migrated_package_cli_tests <phoenix_run_migrated_package_exe>\n";
        return EXIT_FAILURE;
    }

    if (!test_missing_handler_is_reported(argv[1])) {
        std::cerr << "run migrated package CLI tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
