#include "phoenix/migration/production_label_registry.hpp"

#include <regex>
#include <set>

namespace phoenix::migration {
namespace {

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

std::vector<LabelDeclaration> extract_label_declarations(
    const RawProductionFunction& function)
{
    std::vector<LabelDeclaration> declarations;
    const auto labels_text = extract_array_text(function.manifest_text, "labels");
    for (const auto& object : extract_object_texts(labels_text)) {
        const auto uid = extract_string_value(object, "id");
        if (uid.empty()) continue;
        const auto visible = extract_bool_value(object, "visible", true);
        declarations.push_back(LabelDeclaration{
            uid,
            LabelDefinition{
                extract_string_value(object, "name"),
                extract_string_value(object, "color"),
                extract_string_value(object, "material"),
                !visible},
            function.candidate.manifest_path.string()});
    }
    return declarations;
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

std::vector<std::string> extract_string_values(const std::string& text, const std::string& key)
{
    std::vector<std::string> values;
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*"([^"]+)")regex"};
    for (std::sregex_iterator it{text.begin(), text.end(), pattern}, end; it != end; ++it) {
        values.push_back((*it)[1].str());
    }
    return values;
}

std::string function_uid_part(const FunctionId& id)
{
    const auto at = id.find('@');
    return at == std::string::npos ? std::string{} : id.substr(at + 1);
}

std::vector<LabelUid> extract_referenced_label_uids(const RawProductionFunction& function)
{
    std::set<LabelUid> references;
    const std::regex guid_pattern{
        R"([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})"};
    for (std::sregex_iterator it{function.nodes_text.begin(), function.nodes_text.end(), guid_pattern}, end;
         it != end;
         ++it) {
        references.insert((*it)[0].str());
    }

    for (const auto& file_value : extract_string_values(function.nodes_text, "file")) {
        if (looks_like_guid(file_value)) references.erase(file_value);
        if (looks_like_function_id(file_value)) references.erase(function_uid_part(file_value));
    }
    for (const auto& child_id : function.candidate.referenced_function_ids) {
        references.erase(function_uid_part(child_id));
    }
    for (const auto& payload : function.payload_blobs) {
        references.erase(payload.first);
    }

    return {references.begin(), references.end()};
}

} // namespace

ProductionLabelRegistryBuild ProductionLabelRegistryBuilder::build(
    const RawProductionProjectSet& raw) const
{
    ProductionLabelRegistryBuild result;
    LabelFunctionLibrary function_library;
    FunctionDescriptor root;
    bool have_root = false;

    for (const auto& project : raw.projects) {
        if (have_root) continue;
        root.id = project.candidate.id;
        have_root = true;
    }

    for (const auto& entry : raw.functions) {
        for (const auto& function : entry.second) {
            FunctionDescriptor descriptor;
            descriptor.id = function.candidate.id;
            for (const auto& child_id : function.candidate.referenced_function_ids) {
                InstructionDescriptor instruction;
                instruction.id = static_cast<NodeId>(descriptor.instructions.size() + 1);
                instruction.called_function_id = child_id;
                descriptor.instructions.push_back(std::move(instruction));
            }
            auto referenced_label_uids = extract_referenced_label_uids(function);
            if (!referenced_label_uids.empty()) {
                InstructionDescriptor instruction;
                instruction.id = static_cast<NodeId>(descriptor.instructions.size() + 1);
                instruction.kind = "migration_label_references";
                instruction.referenced_label_uids = std::move(referenced_label_uids);
                descriptor.instructions.push_back(std::move(instruction));
            }
            function_library.emplace(descriptor.id, descriptor);

            auto declarations = extract_label_declarations(function);
            if (!declarations.empty()) {
                auto& target = result.declarations[function.candidate.id];
                target.insert(target.end(), declarations.begin(), declarations.end());
            }
        }
    }

    if (!have_root && !function_library.empty()) {
        root = function_library.begin()->second;
        have_root = true;
    }

    if (have_root) {
        if (const auto found = function_library.find(root.id); found != function_library.end()) {
            root = found->second;
        }
        result.linked_labels = LabelLinker{}.link(root, function_library, result.declarations);
    }

    return result;
}

} // namespace phoenix::migration
