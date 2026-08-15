#include "phoenix/migration/production_profile_registry.hpp"

#include <regex>

namespace phoenix::migration {
namespace {

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

std::string canonical_json_text(const std::string& text)
{
    std::string canonical;
    canonical.reserve(text.size());
    bool in_string = false;
    bool escaped = false;
    for (const auto ch : text) {
        if (escaped) {
            canonical.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            canonical.push_back(ch);
            escaped = true;
            continue;
        }
        if (ch == '"') {
            canonical.push_back(ch);
            in_string = !in_string;
            continue;
        }
        if (!in_string && (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')) {
            continue;
        }
        canonical.push_back(ch);
    }
    return canonical;
}

std::vector<ProductionProfileDefinition> extract_profile_declarations(
    const RawProductionFunction& function)
{
    std::vector<ProductionProfileDefinition> declarations;
    const auto profiles_text = extract_array_text(function.manifest_text, "profiles");
    for (const auto& object : extract_object_texts(profiles_text)) {
        const auto id = extract_string_value(object, "id");
        if (id.empty()) continue;
        declarations.push_back(ProductionProfileDefinition{
            id,
            extract_string_value(object, "name"),
            canonical_json_text(object),
            function.candidate.id,
            function.candidate.manifest_path});
    }
    return declarations;
}

std::string profile_display_name(const ProductionProfileDefinition& definition)
{
    return definition.name.empty() ? std::string{"<unnamed>"} : definition.name;
}

} // namespace

ProductionProfileRegistryBuild ProductionProfileRegistryBuilder::build(
    const RawProductionProjectSet& raw,
    const ProductionFunctionLinkResult& linked) const
{
    ProductionProfileRegistryBuild result;

    for (const auto& function_id : linked.reachable_function_ids) {
        const auto found = raw.functions.find(function_id);
        if (found == raw.functions.end()) continue;

        for (const auto& function : found->second) {
            auto declarations = extract_profile_declarations(function);
            if (declarations.empty()) continue;

            auto& function_declarations = result.declarations[function.candidate.id];
            for (const auto& declaration : declarations) {
                function_declarations.push_back(declaration);

                const auto existing = result.profiles.find(declaration.id);
                if (existing == result.profiles.end()) {
                    result.profiles.emplace(declaration.id, declaration);
                    continue;
                }
                if (existing->second.canonical_text == declaration.canonical_text) {
                    continue;
                }

                result.diagnostics.push_back(ProductionProfileDiagnostic{
                    ProductionProfileDiagnosticCode::conflicting_definition,
                    "Profile ID '" + declaration.id + "' has conflicting definitions "
                        "existing_name='" + profile_display_name(existing->second)
                        + "' incoming_name='" + profile_display_name(declaration) + "'.",
                    declaration.function_id,
                    declaration.id,
                    declaration.name,
                    declaration.path});
            }
        }
    }

    return result;
}

std::string to_string(ProductionProfileDiagnosticCode code)
{
    switch (code) {
    case ProductionProfileDiagnosticCode::conflicting_definition: return "conflicting_definition";
    }
    return "unknown";
}

} // namespace phoenix::migration
