#include "phoenix/migration/production_migrated_package_io.hpp"

#include "phoenix/migration/migrated_package_runtime_loader.hpp"

#include <filesystem>
#include <iostream>

namespace {

void print_usage()
{
    std::cerr << "usage: phoenix_validate_package <package.phxmig>\n";
}

std::size_t instruction_count(const phoenix::migration::MigratedProjectPackage& package)
{
    std::size_t count = 0;
    for (const auto& entry : package.functions) {
        count += entry.second.graph.instructions.size();
    }
    return count;
}

std::size_t edge_count(const phoenix::migration::MigratedProjectPackage& package)
{
    std::size_t count = 0;
    for (const auto& entry : package.functions) {
        count += entry.second.graph.edges.size();
    }
    return count;
}

std::size_t edge_count(const phoenix::migration::LoadedMigratedPackage& package)
{
    std::size_t count = 0;
    for (const auto& entry : package.functions) {
        count += entry.second.graph.edges.size();
    }
    return count;
}

std::size_t payload_count(const phoenix::migration::MigratedProjectPackage& package)
{
    std::size_t count = 0;
    for (const auto& entry : package.functions) {
        count += entry.second.payloads.size();
    }
    return count;
}

bool validate_package(const phoenix::migration::MigratedProjectPackage& package)
{
    bool ok = true;
    if (package.functions.find(package.root_function_id) == package.functions.end()) {
        std::cerr << "error package.root_missing function=" << package.root_function_id << "\n";
        ok = false;
    }

    for (const auto& entry : package.functions) {
        const auto& function_id = entry.first;
        const auto& function = entry.second;
        if (function.graph.id != function_id) {
            std::cerr << "error package.graph_id_mismatch function=" << function_id
                      << " graph=" << function.graph.id << "\n";
            ok = false;
        }
        for (const auto& instruction : function.graph.instructions) {
            if (instruction.called_function_id
                && package.functions.find(*instruction.called_function_id) == package.functions.end()) {
                std::cerr << "error package.unresolved_call function=" << function_id
                          << " node=" << instruction.id
                          << " callee=" << *instruction.called_function_id << "\n";
                ok = false;
            }
            if (instruction.configuration_revision.rfind("payload:", 0) == 0) {
                const auto payload_id = instruction.configuration_revision.substr(8);
                if (function.payloads.find(payload_id) == function.payloads.end()) {
                    std::cerr << "error package.unresolved_payload function=" << function_id
                              << " node=" << instruction.id
                              << " payload=" << payload_id << "\n";
                    ok = false;
                }
            }
        }

    }

    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        print_usage();
        return 2;
    }

    const auto path = std::filesystem::path{argv[1]};
    const phoenix::migration::MigratedProjectPackageReader reader;
    const auto read = reader.read(path);
    if (!read.ok()) {
        for (const auto& diagnostic : read.diagnostics) {
            std::cerr << "error package_io." << phoenix::migration::to_string(diagnostic.code)
                      << ": " << diagnostic.message << "\n";
        }
        return 1;
    }

    std::cout << "schema: " << read.package.schema_version << "\n";
    std::cout << "root: " << read.package.root_function_id << "\n";
    std::cout << "functions: " << read.package.functions.size() << "\n";
    std::cout << "instructions: " << instruction_count(read.package) << "\n";
    std::cout << "edges: " << edge_count(read.package) << "\n";
    std::cout << "payloads: " << payload_count(read.package) << "\n";
    if (!validate_package(read.package)) return 1;
    const phoenix::migration::MigratedPackageRuntimeLoader loader;
    const auto loaded = loader.load(read.package);
    if (!loaded.ok()) {
        for (const auto& diagnostic : loaded.diagnostics) {
            std::cerr << "error package_load." << phoenix::migration::to_string(diagnostic.code)
                      << " function=" << diagnostic.function_id
                      << ": " << diagnostic.message << "\n";
        }
        return 1;
    }

    std::cout << "normalized_edges: " << edge_count(loaded.package) << "\n";
    std::cout << "graph_validation: ok\n";
    std::cout << "validation: ok\n";
    return 0;
}
