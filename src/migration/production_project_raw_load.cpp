#include "phoenix/migration/production_project_raw_load.hpp"

#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace phoenix::migration {
namespace {

std::string read_file(
    const std::filesystem::path& path,
    std::vector<RawLoadDiagnostic>& diagnostics,
    const FunctionId& function_id,
    RawLoadDiagnosticCode code = RawLoadDiagnosticCode::unreadable_file,
    const std::string& message = "Could not read production project file.")
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics.push_back(RawLoadDiagnostic{code, message, path, function_id});
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool looks_like_guid(const std::string& value)
{
    static const std::regex pattern{
        R"(^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$)"};
    return std::regex_match(value, pattern);
}

bool looks_like_function_id(const std::string& value)
{
    static const std::regex pattern{
        R"(^[^@\\/:*?"<>|]+@[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$)"};
    return std::regex_match(value, pattern);
}

std::vector<std::string> extract_file_values(const std::string& text)
{
    std::vector<std::string> values;
    const std::regex pattern{R"regex("file"\s*:\s*"([^"]+)")regex"};
    for (std::sregex_iterator it{text.begin(), text.end(), pattern}, end; it != end; ++it) {
        values.push_back((*it)[1].str());
    }
    return values;
}

} // namespace

RawProductionProjectSet ProductionProjectRawLoader::load(
    const std::vector<std::filesystem::path>& roots) const
{
    return load(ProductionProjectDiscoverer{}.discover(roots));
}

RawProductionProjectSet ProductionProjectRawLoader::load(
    ProductionProjectDiscovery discovery) const
{
    RawProductionProjectSet result;
    result.discovery = std::move(discovery);

    for (const auto& project : result.discovery.projects) {
        result.projects.push_back(RawProductionProject{project});
    }

    for (const auto& entry : result.discovery.functions) {
        auto& raw_candidates = result.functions[entry.first];
        for (const auto& candidate : entry.second) {
            RawProductionFunction raw;
            raw.candidate = candidate;
            raw.manifest_text = read_file(candidate.manifest_path, result.diagnostics, candidate.id);
            if (std::filesystem::exists(candidate.nodes_path)) {
                raw.nodes_text = read_file(candidate.nodes_path, result.diagnostics, candidate.id);
            }

            std::set<std::string> payload_ids;
            for (const auto& file_value : extract_file_values(raw.nodes_text)) {
                if (!looks_like_guid(file_value) || looks_like_function_id(file_value)) continue;
                payload_ids.insert(file_value);
            }

            for (const auto& payload_id : payload_ids) {
                const auto payload_path = candidate.manifest_path.parent_path() / payload_id;
                if (!std::filesystem::exists(payload_path)) {
                    result.diagnostics.push_back(RawLoadDiagnostic{
                        RawLoadDiagnosticCode::missing_payload_blob,
                        "Node graph references a missing instruction payload blob.",
                        payload_path,
                        candidate.id});
                    continue;
                }
                RawProductionPayloadBlob blob;
                blob.id = payload_id;
                blob.path = payload_path;
                blob.text = read_file(payload_path, result.diagnostics, candidate.id);
                raw.payload_blobs.emplace(payload_id, std::move(blob));
            }

            raw_candidates.push_back(std::move(raw));
        }
    }

    return result;
}

std::string to_string(RawLoadDiagnosticCode code)
{
    switch (code) {
    case RawLoadDiagnosticCode::unreadable_file: return "unreadable_file";
    case RawLoadDiagnosticCode::missing_payload_blob: return "missing_payload_blob";
    }
    return "unknown";
}

} // namespace phoenix::migration
