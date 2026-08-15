#include "phoenix/migration/production_project_audit.hpp"

#include <fstream>
#include <regex>
#include <sstream>

namespace phoenix::migration {
namespace {

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

std::string function_uid_part(const FunctionId& id)
{
    const auto at = id.find('@');
    return at == std::string::npos ? std::string{} : id.substr(at + 1);
}

std::string read_file(
    const std::filesystem::path& path,
    std::vector<AuditDiagnostic>& diagnostics,
    const FunctionId& function_id = {})
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics.push_back(AuditDiagnostic{
            AuditDiagnosticCode::unreadable_file,
            "Could not read production project file.",
            path,
            function_id,
            {}});
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string extract_string_value(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*"([^"]*)")regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    return it == std::sregex_iterator{} ? std::string{} : (*it)[1].str();
}

bool extract_bool_value(const std::string& text, const std::string& key, bool default_value)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*(true|false))regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    if (it == std::sregex_iterator{}) return default_value;
    return (*it)[1].str() == "true";
}

std::vector<std::string> extract_string_values(const std::string& text, const std::string& key)
{
    std::vector<std::string> values;
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*"([^"]+)")regex"};
    for (std::sregex_iterator it{text.begin(), text.end(), pattern}, end; it != end; ++it) {
        values.push_back((*it)[1].str());
    }
    return values;
}

std::string extract_array_text(const std::string& text, const std::string& key)
{
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return {};
    const auto array_start = text.find('[', key_pos);
    if (array_start == std::string::npos) return {};

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = array_start; index < text.size(); ++index) {
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
        if (ch == '[') ++depth;
        if (ch == ']') {
            --depth;
            if (depth == 0) return text.substr(array_start + 1, index - array_start - 1);
        }
    }
    return {};
}

std::vector<std::string> extract_object_texts(const std::string& text)
{
    std::vector<std::string> objects;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t object_start = std::string::npos;

    for (std::size_t index = 0; index < text.size(); ++index) {
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
        if (ch == '{') {
            if (depth == 0) object_start = index;
            ++depth;
        }
        if (ch == '}') {
            --depth;
            if (depth == 0 && object_start != std::string::npos) {
                objects.push_back(text.substr(object_start, index - object_start + 1));
                object_start = std::string::npos;
            }
        }
    }
    return objects;
}

std::vector<ProductionLabelOccurrence> extract_labels(
    const std::string& manifest_text,
    const FunctionId& function_id,
    const std::filesystem::path& source_path)
{
    std::vector<ProductionLabelOccurrence> labels;
    const auto labels_text = extract_array_text(manifest_text, "labels");
    for (const auto& object : extract_object_texts(labels_text)) {
        const auto uid = extract_string_value(object, "id");
        if (uid.empty()) continue;
        const auto visible = extract_bool_value(object, "visible", true);
        labels.push_back(ProductionLabelOccurrence{
            uid,
            LabelDefinition{
                extract_string_value(object, "name"),
                extract_string_value(object, "color"),
                extract_string_value(object, "material"),
                !visible},
            function_id,
            source_path});
    }
    return labels;
}

std::set<LabelUid> extract_guid_values(const std::string& text)
{
    std::set<LabelUid> values;
    const std::regex pattern{
        R"([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})"};
    for (std::sregex_iterator it{text.begin(), text.end(), pattern}, end; it != end; ++it) {
        values.insert((*it)[0].str());
    }
    return values;
}

} // namespace

ProductionProjectAudit ProductionProjectAuditor::audit(
    const std::vector<std::filesystem::path>& roots) const
{
    ProductionProjectAudit result;
    result.discovery = ProductionProjectDiscoverer{}.discover(roots);
    result.summary.project_manifest_count = result.discovery.projects.size();
    for (const auto& entry : result.discovery.functions) {
        result.summary.function_manifest_count += entry.second.size();
        for (const auto& function : entry.second) {
            if (std::filesystem::exists(function.nodes_path)) {
                ++result.summary.nodes_file_count;
            }
        }
    }
    result.summary.unresolved_imported_function_count =
        result.discovery.unresolved_imported_function_ids.size();

    for (const auto& entry : result.discovery.functions) {
        for (const auto& function : entry.second) {
            const auto manifest_text = read_file(function.manifest_path, result.diagnostics, function.id);
            if (manifest_text.empty()) continue;

            std::set<LabelUid> declared_labels;
            for (const auto& label : extract_labels(manifest_text, function.id, function.manifest_path)) {
                declared_labels.insert(label.uid);
                auto& occurrences = result.labels_by_uid[label.uid];
                if (!occurrences.empty() && !(occurrences.front().definition == label.definition)) {
                    result.diagnostics.push_back(AuditDiagnostic{
                        AuditDiagnosticCode::conflicting_label_definition,
                        "Label UID has conflicting production definitions.",
                        label.source_path,
                        label.function_id,
                        label.uid});
                }
                occurrences.push_back(label);
            }

            if (!std::filesystem::exists(function.nodes_path)) continue;
            const auto nodes_text = read_file(function.nodes_path, result.diagnostics, function.id);
            if (nodes_text.empty()) continue;

            for (const auto& file_value : extract_string_values(nodes_text, "file")) {
                if (looks_like_function_id(file_value)) {
                    ++result.function_call_counts[function.id][file_value];
                    declared_labels.insert(function_uid_part(file_value));
                } else if (looks_like_guid(file_value)) {
                    result.instruction_payload_blobs[function.id].insert(file_value);
                }
            }

            const auto guids = extract_guid_values(nodes_text);
            for (const auto& uid : guids) {
                if (declared_labels.count(uid) != 0) continue;
                if (result.instruction_payload_blobs[function.id].count(uid) != 0) continue;
                result.unresolved_label_references[function.id].insert(uid);
                result.diagnostics.push_back(AuditDiagnostic{
                    AuditDiagnosticCode::unresolved_label_reference,
                    "Node graph references a GUID not declared as a function-local label.",
                    function.nodes_path,
                    function.id,
                    uid});
            }
        }
    }

    for (const auto& entry : result.labels_by_uid) {
        if (entry.second.size() <= 1) continue;
        ++result.summary.duplicate_label_uid_count;
    }

    std::set<LabelUid> conflicting_labels;
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == AuditDiagnosticCode::conflicting_label_definition) {
            conflicting_labels.insert(diagnostic.label_uid);
        }
    }
    result.summary.conflicting_label_uid_count = conflicting_labels.size();

    for (const auto& entry : result.function_call_counts) {
        for (const auto& call : entry.second) {
            const bool local = result.discovery.functions.find(call.first) != result.discovery.functions.end();
            if (local) {
                result.summary.local_function_reference_count += call.second;
            } else {
                result.summary.imported_function_reference_count += call.second;
            }
            if (call.second > 1) {
                result.summary.repeated_function_call_site_count += call.second;
            }
        }
    }

    for (const auto& entry : result.instruction_payload_blobs) {
        result.summary.instruction_payload_blob_reference_count += entry.second.size();
    }
    for (const auto& entry : result.unresolved_label_references) {
        result.summary.unresolved_label_reference_count += entry.second.size();
    }

    return result;
}

std::string to_string(AuditDiagnosticCode code)
{
    switch (code) {
    case AuditDiagnosticCode::conflicting_label_definition: return "conflicting_label_definition";
    case AuditDiagnosticCode::unresolved_label_reference: return "unresolved_label_reference";
    case AuditDiagnosticCode::unreadable_file: return "unreadable_file";
    }
    return "unknown";
}

} // namespace phoenix::migration
