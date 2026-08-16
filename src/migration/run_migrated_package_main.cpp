#include "phoenix/execution.hpp"
#include "phoenix/migration/migrated_instruction_registry.hpp"
#include "phoenix/migration/migrated_package_runtime_loader.hpp"
#include "phoenix/migration/production_migrated_package_io.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>

namespace {

void print_usage()
{
    std::cerr << "usage: phoenix_run_migrated_package <package.phxmig>\n";
}

std::set<std::string> instruction_kinds(const phoenix::migration::LoadedMigratedPackage& package)
{
    std::set<std::string> kinds;
    for (const auto& entry : package.functions) {
        for (const auto& instruction : entry.second.graph.instructions) {
            if (instruction.kind != "output" && !instruction.called_function_id.has_value()) {
                kinds.insert(instruction.kind);
            }
        }
    }
    return kinds;
}

bool report_missing_handlers(
    const phoenix::migration::LoadedMigratedPackage& package,
    const phoenix::InstructionRegistry& registry)
{
    bool missing = false;
    for (const auto& kind : instruction_kinds(package)) {
        if (registry.find_handler(kind) == nullptr) {
            std::cerr << "error runtime.missing_handler kind=" << kind << "\n";
            missing = true;
        }
    }
    return missing;
}

phoenix::FunctionLibrary make_function_library(
    const phoenix::migration::LoadedMigratedPackage& package)
{
    phoenix::FunctionLibrary library;
    for (const auto& entry : package.functions) {
        library.register_function(entry.second.graph);
    }
    return library;
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

    auto migrated_registry = phoenix::migration::make_migrated_instruction_registry(loaded.package);
    if (report_missing_handlers(loaded.package, migrated_registry.registry)) {
        std::cerr << "runtime_attempt: blocked\n";
        return 1;
    }
    if (!migrated_registry.unsupported_kinds.empty()) {
        for (const auto& kind : migrated_registry.unsupported_kinds) {
            std::cerr << "error runtime.unsupported_handler kind=" << kind << "\n";
            const auto total = migrated_registry.total_by_kind.find(kind);
            const auto adapted = migrated_registry.adapted_by_kind.find(kind);
            if (total != migrated_registry.total_by_kind.end()
                && adapted != migrated_registry.adapted_by_kind.end()
                && adapted->second > 0) {
                std::cerr << "info runtime.adapter_coverage kind=" << kind
                          << " adapted=" << adapted->second
                          << " total=" << total->second << "\n";
            }
            const auto reasons = migrated_registry.unsupported_reasons_by_kind.find(kind);
            if (reasons != migrated_registry.unsupported_reasons_by_kind.end()) {
                for (const auto& reason : reasons->second) {
                    std::cerr << "info runtime.unsupported_reason kind=" << kind
                              << " reason=" << reason.first
                              << " count=" << reason.second << "\n";
                }
            }
            const auto examples = migrated_registry.unsupported_examples_by_kind.find(kind);
            if (examples != migrated_registry.unsupported_examples_by_kind.end()) {
                for (const auto& example : examples->second) {
                    std::cerr << "info runtime.unsupported_example kind=" << kind
                              << " " << example << "\n";
                }
            }
        }
        std::cerr << "runtime_attempt: blocked\n";
        return 1;
    }

    const auto root_it = loaded.package.functions.find(loaded.package.root_function_id);
    if (root_it == loaded.package.functions.end()) {
        std::cerr << "error runtime.root_missing function=" << loaded.package.root_function_id << "\n";
        return 1;
    }

    auto library = make_function_library(loaded.package);
    phoenix::FunctionExecutionRequest request;
    request.function = &root_it->second.graph;
    request.context.function_id = root_it->second.graph.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 13;

    const phoenix::FunctionExecutor executor(migrated_registry.registry, library);
    const auto result = executor.run(request);
    if (result.status != phoenix::FunctionExecutionStatus::completed) {
        std::cerr << "error runtime.execution_failed status=" << phoenix::to_string(result.status);
        if (result.failure_message.has_value()) {
            std::cerr << ": " << *result.failure_message;
        }
        std::cerr << "\n";
        std::cerr << "runtime_attempt: blocked\n";
        return 1;
    }

    std::cout << "runtime_attempt: ok\n";
    std::cout << "outputs: " << result.outputs.size() << "\n";
    return 0;
}
