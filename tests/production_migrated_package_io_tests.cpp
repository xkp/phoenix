#include "phoenix/migration/production_migrated_package_io.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_package_io_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

bool test_round_trips_migrated_package()
{
    const auto root = temp_root();
    const auto path = root / "package.phxmig";

    phoenix::migration::MigratedProjectPackage package;
    package.root_function_id = "ROOT@11111111-1111-1111-1111-111111111111";
    package.label_registry_fingerprint = 42;

    phoenix::migration::MigratedFunctionPackage function;
    function.graph.id = package.root_function_id;
    function.manifest_path = "manifest path";
    function.nodes_path = "nodes path";
    function.origins.push_back("origin path");
    function.fingerprint = 99;
    function.graph.input_ports.push_back({"0:input", "geometry", phoenix::PortDirection::input});
    phoenix::InstructionDescriptor instruction;
    instruction.id = 7;
    instruction.kind = "partition";
    instruction.configuration_revision = "payload:PAYLOAD";
    instruction.output_ports.push_back({"0:output", "geometry", phoenix::PortDirection::output});
    function.graph.instructions.push_back(instruction);
    function.graph.edges.push_back({7, "0:output", 8, "0:input"});
    function.payloads.emplace("PAYLOAD", phoenix::migration::MigratedInstructionPayload{
        "PAYLOAD",
        "payload path",
        "line one\nvalue|with pipe"});
    package.functions.emplace(package.root_function_id, function);

    const phoenix::migration::MigratedProjectPackageWriter writer;
    const auto write_diagnostics = writer.write(package, path);
    const phoenix::migration::MigratedProjectPackageReader reader;
    const auto read = reader.read(path);

    const auto& round_trip = read.package.functions.at(package.root_function_id);
    return write_diagnostics.empty()
        && read.ok()
        && read.package.root_function_id == package.root_function_id
        && read.package.label_registry_fingerprint == 42
        && round_trip.graph.instructions.size() == 1
        && round_trip.graph.instructions.front().kind == "partition"
        && round_trip.graph.edges.size() == 1
        && round_trip.payloads.at("PAYLOAD").text == "line one\nvalue|with pipe";
}

bool test_reader_rejects_invalid_header()
{
    const auto root = temp_root();
    const auto path = root / "bad.phxmig";
    {
        std::ofstream output(path, std::ios::binary);
        output << "bad\n";
    }

    const phoenix::migration::MigratedProjectPackageReader reader;
    const auto read = reader.read(path);
    return !read.ok()
        && read.diagnostics.front().code == phoenix::migration::PackageIoDiagnosticCode::invalid_format;
}

} // namespace

int main()
{
    const bool ok = test_round_trips_migrated_package()
        && test_reader_rejects_invalid_header();
    if (!ok) {
        std::cerr << "production migrated package io tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
