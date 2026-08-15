#include "phoenix/migration/production_function_linker.hpp"

#include <cctype>
#include <deque>
#include <regex>

namespace phoenix::migration {
namespace {

void hash_byte(std::uint64_t& hash, unsigned char value) noexcept
{
    hash ^= value;
    hash *= 1099511628211ULL;
}

void hash_string(std::uint64_t& hash, const std::string& value) noexcept
{
    for (const auto ch : value) hash_byte(hash, static_cast<unsigned char>(ch));
    hash_byte(hash, 0xffU);
}

std::uint64_t fingerprint(const RawProductionFunction& function) noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    hash_string(hash, function.manifest_text);
    hash_string(hash, function.nodes_text);
    for (const auto& entry : function.payload_blobs) {
        hash_string(hash, entry.first);
        hash_string(hash, entry.second.text);
    }
    return hash;
}

std::string remove_top_level_property(const std::string& text, const std::string& key)
{
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return text;

    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    std::size_t property_start = key_pos;
    while (property_start > 0 && std::isspace(static_cast<unsigned char>(text[property_start - 1]))) {
        --property_start;
    }
    bool removed_preceding_comma = false;
    if (property_start > 0 && text[property_start - 1] == ',') {
        --property_start;
        removed_preceding_comma = true;
    }

    std::size_t colon = std::string::npos;
    for (std::size_t index = key_pos; index < text.size(); ++index) {
        const auto ch = text[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (!in_string && ch == ':') {
            colon = index;
            break;
        }
    }
    if (colon == std::string::npos) return text;

    in_string = false;
    escaped = false;
    std::size_t property_end = text.size();
    for (std::size_t index = colon + 1; index < text.size(); ++index) {
        const auto ch = text[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (ch == '{' || ch == '[') ++depth;
        if (ch == '}' || ch == ']') {
            if (depth == 0) {
                property_end = index;
                break;
            }
            --depth;
        }
        if (ch == ',' && depth == 0) {
            property_end = removed_preceding_comma ? index : index + 1;
            break;
        }
    }

    auto result = text;
    result.erase(property_start, property_end - property_start);
    return result;
}

std::string canonical_manifest_for_fingerprint(std::string text)
{
    text = remove_top_level_property(text, "selfContained");
    std::string result;
    bool in_string = false;
    bool escaped = false;
    for (const auto ch : text) {
        if (escaped) {
            result += ch;
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            result += ch;
            escaped = true;
            continue;
        }
        if (ch == '"') {
            result += ch;
            in_string = !in_string;
            continue;
        }
        if (!in_string && std::isspace(static_cast<unsigned char>(ch))) continue;
        result += ch;
    }
    return result;
}

std::uint64_t semantic_fingerprint(const RawProductionFunction& function) noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    hash_string(hash, canonical_manifest_for_fingerprint(function.manifest_text));
    hash_string(hash, function.nodes_text);
    for (const auto& entry : function.payload_blobs) {
        hash_string(hash, entry.first);
        hash_string(hash, entry.second.text);
    }
    return hash;
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

ProductionFunctionLinkResult ProductionFunctionLinker::link(
    const RawProductionProjectSet& raw) const
{
    ProductionFunctionLinkResult result;
    std::deque<FunctionId> pending;

    for (const auto& project : raw.projects) {
        pending.push_back(project.candidate.id);
    }
    if (pending.empty() && !raw.functions.empty()) {
        pending.push_back(raw.functions.begin()->first);
    }

    while (!pending.empty()) {
        const auto id = pending.front();
        pending.pop_front();
        if (!result.reachable_function_ids.insert(id).second) continue;

        const auto candidates_it = raw.functions.find(id);
        if (candidates_it == raw.functions.end() || candidates_it->second.empty()) {
            result.unresolved_function_ids.insert(id);
            result.diagnostics.push_back(FunctionLinkDiagnostic{
                FunctionLinkDiagnosticCode::unresolved_function_reference,
                "Function reference could not be resolved to a production definition.",
                id,
                {}});
            continue;
        }

        const auto first_fingerprint = fingerprint(candidates_it->second.front());
        const auto first_semantic_fingerprint = semantic_fingerprint(candidates_it->second.front());
        LinkedProductionFunction linked;
        linked.id = id;
        linked.function = candidates_it->second.front();
        linked.fingerprint = first_fingerprint;
        linked.origins.insert(linked.function.candidate.manifest_path);

        for (std::size_t index = 1; index < candidates_it->second.size(); ++index) {
            const auto candidate_semantic_fingerprint = semantic_fingerprint(candidates_it->second[index]);
            if (candidate_semantic_fingerprint != first_semantic_fingerprint) {
                result.diagnostics.push_back(FunctionLinkDiagnostic{
                    FunctionLinkDiagnosticCode::conflicting_function_definition,
                    "Function ID has multiple production definitions with different content.",
                    id,
                    candidates_it->second[index].candidate.manifest_path});
                continue;
            }
            linked.origins.insert(candidates_it->second[index].candidate.manifest_path);
        }

        for (const auto& file_value : extract_file_values(linked.function.nodes_text)) {
            if (!looks_like_function_id(file_value)) continue;
            ++result.call_counts[id][file_value];
        }
        for (const auto& child_id : linked.function.candidate.referenced_function_ids) {
            if (result.call_counts[id].find(child_id) == result.call_counts[id].end()) {
                result.call_counts[id][child_id] = 1;
            }
        }
        for (const auto& call_entry : result.call_counts[id]) {
            result.references.push_back(ProductionFunctionReference{
                id,
                call_entry.first,
                call_entry.second});
        }

        for (const auto& child_id : linked.function.candidate.referenced_function_ids) {
            pending.push_back(child_id);
        }
        result.functions.emplace(id, std::move(linked));
    }

    return result;
}

std::string to_string(FunctionLinkDiagnosticCode code)
{
    switch (code) {
    case FunctionLinkDiagnosticCode::conflicting_function_definition: return "conflicting_function_definition";
    case FunctionLinkDiagnosticCode::unresolved_function_reference: return "unresolved_function_reference";
    }
    return "unknown";
}

} // namespace phoenix::migration
