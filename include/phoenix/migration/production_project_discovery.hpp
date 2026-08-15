#pragma once

#include "phoenix/common.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace phoenix::migration {

enum class DiscoveryDiagnosticCode {
    missing_nodes_file,
    unreadable_file,
};

struct DiscoveryDiagnostic {
    DiscoveryDiagnosticCode code;
    std::string message;
    std::filesystem::path path;
    FunctionId function_id;
};

struct ProductionFunctionCandidate {
    FunctionId id;
    std::string name;
    bool imported = false;
    std::filesystem::path manifest_path;
    std::filesystem::path nodes_path;
    std::set<FunctionId> referenced_function_ids;
};

struct ProductionProjectCandidate {
    FunctionId id;
    std::string name;
    std::filesystem::path directory;
    std::filesystem::path manifest_path;
    std::filesystem::path nodes_path;
    std::set<FunctionId> declared_function_ids;
};

struct ProductionProjectDiscovery {
    std::vector<ProductionProjectCandidate> projects;
    std::map<FunctionId, std::vector<ProductionFunctionCandidate>> functions;
    std::set<FunctionId> imported_function_references;
    std::set<FunctionId> unresolved_imported_function_ids;
    std::vector<DiscoveryDiagnostic> diagnostics;
};

class ProductionProjectDiscoverer {
public:
    [[nodiscard]] ProductionProjectDiscovery discover(
        const std::vector<std::filesystem::path>& roots) const;
};

[[nodiscard]] std::string to_string(DiscoveryDiagnosticCode code);

} // namespace phoenix::migration
