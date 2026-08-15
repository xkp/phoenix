#include "phoenix/migration/production_migration_overrides.hpp"

#include <regex>

namespace phoenix::migration {
namespace {

std::size_t replace_all(std::string& text, const std::string& from, const std::string& to)
{
    if (from.empty()) return 0;
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
        ++count;
    }
    return count;
}

void rewrite_references(
    std::set<FunctionId>& references,
    const FunctionId& from,
    const FunctionId& to)
{
    const auto erased = references.erase(from);
    if (erased != 0) references.insert(to);
}

std::string extract_string_value(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*"([^"]*)")regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    return it == std::sregex_iterator{} ? std::string{} : (*it)[1].str();
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

struct ObjectRange {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string text;
};

std::vector<ObjectRange> extract_object_ranges(const std::string& text)
{
    std::vector<ObjectRange> objects;
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
                objects.push_back(ObjectRange{
                    object_start,
                    index + 1,
                    text.substr(object_start, index - object_start + 1)});
                object_start = std::string::npos;
            }
        }
    }
    return objects;
}

std::size_t replace_array_objects_by_id(
    std::string& text,
    const std::string& array_key,
    const std::string& id,
    const std::string& replacement)
{
    const auto key_pos = text.find("\"" + array_key + "\"");
    if (key_pos == std::string::npos) return 0;
    const auto array_start = text.find('[', key_pos);
    if (array_start == std::string::npos) return 0;
    const auto array_text = extract_array_text(text, array_key);

    auto objects = extract_object_ranges(array_text);
    std::size_t count = 0;
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
        if (extract_string_value(it->text, "id") != id) continue;
        text.replace(array_start + 1 + it->begin, it->end - it->begin, replacement);
        ++count;
    }
    return count;
}

std::size_t append_array_object_if_missing(
    std::string& text,
    const std::string& array_key,
    const std::string& id,
    const std::string& object)
{
    if (text.find("\"" + array_key + "\"") == std::string::npos) {
        const auto insert = ",\"" + array_key + "\":[" + object + "]";
        const auto close = text.rfind('}');
        if (close == std::string::npos) return 0;
        text.insert(close, insert);
        return 1;
    }

    const auto key_pos = text.find("\"" + array_key + "\"");
    const auto array_start = text.find('[', key_pos);
    if (array_start == std::string::npos) return 0;
    const auto array_text = extract_array_text(text, array_key);
    for (const auto& range : extract_object_ranges(array_text)) {
        if (extract_string_value(range.text, "id") == id) return 0;
    }

    const auto array_end = array_start + 1 + array_text.size();
    if (array_text.find_first_not_of(" \t\r\n") == std::string::npos) {
        text.insert(array_end, object);
    } else {
        text.insert(array_end, "," + object);
    }
    return 1;
}

std::string mark_object_disabled(std::string object)
{
    const std::regex disabled_pattern{R"regex("disabled"\s*:\s*(true|false))regex"};
    if (std::regex_search(object, disabled_pattern)) {
        return std::regex_replace(object, disabled_pattern, R"("disabled":true)");
    }
    if (object.size() >= 2 && object.front() == '{') {
        object.insert(1, R"("disabled":true,)");
    }
    return object;
}

std::size_t disable_function_call_nodes(std::string& nodes_text, const FunctionId& function_id)
{
    const auto key_pos = nodes_text.find("\"nodes\"");
    if (key_pos == std::string::npos) return 0;
    const auto array_start = nodes_text.find('[', key_pos);
    if (array_start == std::string::npos) return 0;
    const auto nodes_array = extract_array_text(nodes_text, "nodes");

    auto objects = extract_object_ranges(nodes_array);
    std::size_t count = 0;
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
        if (extract_string_value(it->text, "file") != function_id) continue;
        const auto disabled = mark_object_disabled(it->text);
        nodes_text.replace(array_start + 1 + it->begin, it->end - it->begin, disabled);
        ++count;
    }
    return count;
}

std::string object_with_id(const std::string& id, const std::string& value_json)
{
    if (value_json.size() < 2 || value_json.front() != '{') return value_json;
    if (value_json == "{}") return "{\"id\":\"" + id + "\"}";
    return "{\"id\":\"" + id + "\"," + value_json.substr(1);
}

} // namespace

OverrideApplicationResult ProductionMigrationOverrideApplier::apply(
    RawProductionProjectSet raw,
    const ProductionMigrationOverrides& overrides) const
{
    OverrideApplicationResult result;
    result.raw = std::move(raw);

    for (auto& entry : result.raw.functions) {
        for (auto& function : entry.second) {
            for (const auto& override : overrides.label_uid_remaps) {
                std::size_t count = 0;
                count += replace_all(function.manifest_text, override.from, override.to);
                count += replace_all(function.nodes_text, override.from, override.to);
                for (auto& payload : function.payload_blobs) {
                    count += replace_all(payload.second.text, override.from, override.to);
                }
                if (count != 0) {
                    result.applied.push_back(AppliedMigrationOverride{
                        "label_uid_remap",
                        override.from,
                        override.to,
                        function.candidate.id,
                        count});
                }
            }

            for (const auto& override : overrides.function_reference_rewrites) {
                std::size_t count = 0;
                count += replace_all(function.manifest_text, override.from, override.to);
                count += replace_all(function.nodes_text, override.from, override.to);
                rewrite_references(
                    function.candidate.referenced_function_ids,
                    override.from,
                    override.to);
                if (count != 0) {
                    result.applied.push_back(AppliedMigrationOverride{
                        "function_reference_rewrite",
                        override.from,
                        override.to,
                        function.candidate.id,
                        count});
                }
            }

            for (const auto& override : overrides.ignored_function_references) {
                const auto count = disable_function_call_nodes(
                    function.nodes_text,
                    override.function_id);
                if (count != 0) {
                    function.candidate.referenced_function_ids.erase(override.function_id);
                    result.applied.push_back(AppliedMigrationOverride{
                        "ignored_function_reference",
                        override.function_id,
                        {},
                        function.candidate.id,
                        count});
                }
            }

            for (const auto& override : overrides.label_definition_choices) {
                const auto replacement = object_with_id(override.uid, override.value_json);
                const auto count = override.target_function_id.empty()
                    ? replace_array_objects_by_id(
                        function.manifest_text,
                        "labels",
                        override.uid,
                        replacement)
                    : (function.candidate.id == override.target_function_id
                        ? append_array_object_if_missing(
                            function.manifest_text,
                            "labels",
                            override.uid,
                            replacement)
                        : 0);
                if (count != 0) {
                    result.applied.push_back(AppliedMigrationOverride{
                        "label_definition_choice",
                        override.uid,
                        {},
                        function.candidate.id,
                        count});
                }
            }

            for (const auto& override : overrides.profile_definition_choices) {
                const auto count = replace_array_objects_by_id(
                    function.manifest_text,
                    "profiles",
                    override.profile_id,
                    object_with_id(override.profile_id, override.value_json));
                if (count != 0) {
                    result.applied.push_back(AppliedMigrationOverride{
                        "profile_definition_choice",
                        override.profile_id,
                        {},
                        function.candidate.id,
                        count});
                }
            }
        }
    }

    for (auto& project : result.raw.projects) {
        for (const auto& override : overrides.function_reference_rewrites) {
            rewrite_references(project.candidate.declared_function_ids, override.from, override.to);
        }
    }

    for (const auto& override : overrides.function_reference_rewrites) {
        rewrite_references(
            result.raw.discovery.imported_function_references,
            override.from,
            override.to);
        rewrite_references(
            result.raw.discovery.unresolved_imported_function_ids,
            override.from,
            override.to);
    }

    for (const auto& override : overrides.ignored_function_references) {
        result.raw.discovery.imported_function_references.erase(override.function_id);
        result.raw.discovery.unresolved_imported_function_ids.erase(override.function_id);
        for (auto& project : result.raw.projects) {
            project.candidate.declared_function_ids.erase(override.function_id);
        }
    }

    return result;
}

} // namespace phoenix::migration
