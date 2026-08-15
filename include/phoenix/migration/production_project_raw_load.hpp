#pragma once

#include "phoenix/migration/production_project_discovery.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace phoenix::migration {

enum class RawLoadDiagnosticCode {
    unreadable_file,
    missing_payload_blob,
};

struct RawLoadDiagnostic {
    RawLoadDiagnosticCode code;
    std::string message;
    std::filesystem::path path;
    FunctionId function_id;
};

struct RawProductionPayloadBlob {
    std::string id;
    std::filesystem::path path;
    std::string text;
};

struct RawProductionFunction {
    ProductionFunctionCandidate candidate;
    std::string manifest_text;
    std::string nodes_text;
    std::map<std::string, RawProductionPayloadBlob> payload_blobs;
};

struct RawProductionProject {
    ProductionProjectCandidate candidate;
};

struct RawProductionProjectSet {
    ProductionProjectDiscovery discovery;
    std::vector<RawProductionProject> projects;
    std::map<FunctionId, std::vector<RawProductionFunction>> functions;
    std::vector<RawLoadDiagnostic> diagnostics;
};

class ProductionProjectRawLoader {
public:
    [[nodiscard]] RawProductionProjectSet load(
        const std::vector<std::filesystem::path>& roots) const;
    [[nodiscard]] RawProductionProjectSet load(
        ProductionProjectDiscovery discovery) const;
};

[[nodiscard]] std::string to_string(RawLoadDiagnosticCode code);

} // namespace phoenix::migration
