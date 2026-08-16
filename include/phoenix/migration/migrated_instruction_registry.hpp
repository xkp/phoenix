#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/migration/migrated_package_runtime_loader.hpp"

#include <set>
#include <string>
#include <map>
#include <vector>

namespace phoenix::migration {

struct MigratedInstructionRegistryResult {
    InstructionRegistry registry;
    std::set<std::string> supported_kinds;
    std::set<std::string> unsupported_kinds;
    std::map<std::string, std::size_t> total_by_kind;
    std::map<std::string, std::size_t> adapted_by_kind;
    std::map<std::string, std::map<std::string, std::size_t>> unsupported_reasons_by_kind;
    std::map<std::string, std::vector<std::string>> unsupported_examples_by_kind;
};

[[nodiscard]] MigratedInstructionRegistryResult make_migrated_instruction_registry(
    const LoadedMigratedPackage& package);

} // namespace phoenix::migration
