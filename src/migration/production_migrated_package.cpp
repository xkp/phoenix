#include "phoenix/migration/production_migrated_package.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace phoenix::migration {
namespace {

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

std::uint64_t extract_uint_value(const std::string& text, const std::string& key)
{
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return 0;
    const auto colon = text.find(':', key_pos);
    if (colon == std::string::npos) return 0;
    auto index = colon + 1;
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index]))) ++index;
    std::string digits;
    while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
        digits += text[index++];
    }
    return digits.empty() ? 0 : static_cast<std::uint64_t>(std::stoull(digits));
}

std::string extract_string_value(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*"([^"]*)")regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    return it == std::sregex_iterator{} ? std::string{} : (*it)[1].str();
}

std::optional<double> extract_double_value(const std::string& text, const std::string& key)
{
    const std::regex pattern{
        "\"" + key + R"regex("\s*:\s*(-?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?))regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    if (it == std::sregex_iterator{}) return std::nullopt;
    return std::stod((*it)[1].str());
}

bool extract_bool_value(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*true)regex"};
    return std::regex_search(text, pattern);
}

std::string extract_object_field(const std::string& text, const std::string& key)
{
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return {};
    const auto object_start = text.find('{', key_pos);
    if (object_start == std::string::npos) return {};

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = object_start; index < text.size(); ++index) {
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
        if (ch == '{') ++depth;
        if (ch == '}') {
            --depth;
            if (depth == 0) return text.substr(object_start, index - object_start + 1);
        }
    }
    return {};
}

class NumericExpressionParser {
public:
    NumericExpressionParser(
        std::string_view text,
        const std::map<std::string, double>& variables)
        : text_{text}
        , variables_{variables}
    {
    }

    std::optional<double> parse()
    {
        auto value = expression();
        skip_spaces();
        if (!value.has_value() || pos_ != text_.size()) return std::nullopt;
        return value;
    }

private:
    std::optional<double> expression()
    {
        auto value = term();
        if (!value.has_value()) return std::nullopt;
        while (true) {
            skip_spaces();
            if (match('+')) {
                auto rhs = term();
                if (!rhs.has_value()) return std::nullopt;
                *value += *rhs;
            } else if (match('-')) {
                auto rhs = term();
                if (!rhs.has_value()) return std::nullopt;
                *value -= *rhs;
            } else {
                return value;
            }
        }
    }

    std::optional<double> term()
    {
        auto value = factor();
        if (!value.has_value()) return std::nullopt;
        while (true) {
            skip_spaces();
            if (match('*')) {
                auto rhs = factor();
                if (!rhs.has_value()) return std::nullopt;
                *value *= *rhs;
            } else if (match('/')) {
                auto rhs = factor();
                if (!rhs.has_value() || *rhs == 0.0) return std::nullopt;
                *value /= *rhs;
            } else {
                return value;
            }
        }
    }

    std::optional<double> factor()
    {
        skip_spaces();
        if (match('+')) return factor();
        if (match('-')) {
            auto value = factor();
            if (!value.has_value()) return std::nullopt;
            return -*value;
        }
        if (match('(')) {
            auto value = expression();
            if (!value.has_value() || !match(')')) return std::nullopt;
            return value;
        }
        if (match('[')) {
            const auto start = pos_;
            while (pos_ < text_.size() && text_[pos_] != ']') ++pos_;
            if (pos_ >= text_.size()) return std::nullopt;
            auto value = variable_value(std::string{text_.substr(start, pos_ - start)});
            ++pos_;
            return value;
        }
        if (pos_ < text_.size()
            && (std::isalpha(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_')) {
            const auto start = pos_;
            while (pos_ < text_.size()
                   && (std::isalnum(static_cast<unsigned char>(text_[pos_]))
                       || text_[pos_] == '_')) {
                ++pos_;
            }
            return variable_value(std::string{text_.substr(start, pos_ - start)});
        }
        return number();
    }

    std::optional<double> number()
    {
        const auto start = pos_;
        bool saw_digit = false;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            saw_digit = true;
            ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                saw_digit = true;
                ++pos_;
            }
        }
        if (!saw_digit) return std::nullopt;
        try {
            return std::stod(std::string{text_.substr(start, pos_ - start)});
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<double> variable_value(const std::string& name) const
    {
        const auto found = variables_.find(name);
        return found == variables_.end()
            ? std::optional<double>{}
            : std::optional<double>{found->second};
    }

    bool match(char ch)
    {
        skip_spaces();
        if (pos_ >= text_.size() || text_[pos_] != ch) return false;
        ++pos_;
        return true;
    }

    void skip_spaces()
    {
        while (pos_ < text_.size()
               && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    std::string_view text_;
    const std::map<std::string, double>& variables_;
    std::size_t pos_ = 0;
};

std::map<NodeId, std::string> extract_instruction_node_data(const std::string& nodes_text)
{
    std::map<NodeId, std::string> result;
    const auto raw_nodes = extract_array_text(nodes_text, "nodes");
    for (const auto& object : extract_object_texts(raw_nodes)) {
        const auto node_id = static_cast<NodeId>(extract_uint_value(object, "id"));
        auto data = extract_object_field(object, "data");
        if (!data.empty()) {
            result.emplace(node_id, std::move(data));
        }
    }
    return result;
}

std::map<std::string, double> extract_numeric_variables(const std::string& manifest_text)
{
    std::map<std::string, double> result;
    std::map<std::string, std::string> expressions;
    const auto variables_text = extract_array_text(manifest_text, "variables");
    for (const auto& object : extract_object_texts(variables_text)) {
        const auto name = extract_string_value(object, "name");
        if (name.empty()) continue;
        if (extract_bool_value(object, "isExpression")) {
            const auto expression = extract_string_value(object, "expression");
            if (!expression.empty()) {
                expressions.emplace(name, expression);
            }
            continue;
        }
        const auto value_object = extract_object_field(object, "value");
        const auto value = extract_double_value(value_object.empty() ? object : value_object, "value");
        if (value.has_value()) {
            result.emplace(name, *value);
        }
    }

    bool changed = true;
    while (changed && !expressions.empty()) {
        changed = false;
        for (auto it = expressions.begin(); it != expressions.end();) {
            const auto value = NumericExpressionParser{it->second, result}.parse();
            if (value.has_value()) {
                result.emplace(it->first, *value);
                it = expressions.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    return result;
}

bool looks_like_function_id(const std::string& value)
{
    return value.find('@') != std::string::npos;
}

void apply_call_variable_overrides(
    MigratedProjectPackage& package,
    const std::string& nodes_text)
{
    const auto raw_nodes = extract_array_text(nodes_text, "nodes");
    for (const auto& object : extract_object_texts(raw_nodes)) {
        if (extract_string_value(object, "typeName") != "function") continue;
        const auto data = extract_object_field(object, "data");
        const auto callee = extract_string_value(data, "file");
        if (!looks_like_function_id(callee)) continue;

        auto callee_it = package.functions.find(callee);
        if (callee_it == package.functions.end()) continue;

        const auto variables_text = extract_array_text(data, "variables");
        if (variables_text.empty()) continue;
        const auto overrides = extract_numeric_variables("{\"variables\":[" + variables_text + "]}");
        for (const auto& override : overrides) {
            const auto existing = callee_it->second.numeric_variables.find(override.first);
            if (existing == callee_it->second.numeric_variables.end()) {
                callee_it->second.numeric_variables.emplace(override);
            } else if (existing->second == override.second) {
                continue;
            }
        }
    }
}

void append_retired_instruction_method_diagnostics(
    std::vector<PackageEmissionDiagnostic>& diagnostics,
    const ProductionMigrationReport& report)
{
    for (const auto& entry : report.linked_functions.functions) {
        const auto raw_nodes = extract_array_text(entry.second.function.nodes_text, "nodes");
        for (const auto& object : extract_object_texts(raw_nodes)) {
            if (extract_string_value(object, "typeName") != "extrusion") continue;
            const auto data = extract_object_field(object, "data");
            if (extract_string_value(data, "method") != "label") continue;
            std::ostringstream message;
            message << "Retired extrusion method 'label' is not supported for migration"
                    << " in function " << entry.first
                    << " node " << extract_uint_value(object, "id") << ".";
            diagnostics.push_back(PackageEmissionDiagnostic{
                PackageEmissionDiagnosticCode::retired_instruction_method,
                message.str()});
        }
    }
}

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

} // namespace

PackageEmissionResult MigratedProjectPackageBuilder::build(
    const ProductionMigrationReport& report) const
{
    PackageEmissionResult result;
    if (!report.ok()) {
        result.diagnostics.push_back(PackageEmissionDiagnostic{
            PackageEmissionDiagnosticCode::migration_report_has_errors,
            "Cannot emit migrated project package while migration report has errors."});
        return result;
    }
    append_retired_instruction_method_diagnostics(result.diagnostics, report);
    if (!result.diagnostics.empty()) return result;

    if (!report.discovery.projects.empty()) {
        result.package.root_function_id = report.discovery.projects.front().id;
    } else if (!report.graphs.functions.empty()) {
        result.package.root_function_id = report.graphs.functions.begin()->first;
    }
    result.package.label_registry_fingerprint =
        report.labels.linked_labels.registry.semantic_fingerprint();
    for (const auto& entry : report.labels.declarations) {
        for (const auto& declaration : entry.second) {
            const auto id = report.labels.linked_labels.registry.find_uid(declaration.uid);
            if (id.has_value()) {
                result.package.label_ids.emplace(declaration.uid, id->value());
            }
        }
    }

    for (const auto& entry : report.linked_functions.functions) {
        const auto graph_it = report.graphs.functions.find(entry.first);
        if (graph_it == report.graphs.functions.end()) continue;

        MigratedFunctionPackage function;
        function.graph = graph_it->second;
        function.manifest_path = entry.second.function.candidate.manifest_path;
        function.nodes_path = entry.second.function.candidate.nodes_path;
        function.origins.assign(entry.second.origins.begin(), entry.second.origins.end());
        std::sort(function.origins.begin(), function.origins.end());
        function.fingerprint = entry.second.fingerprint;
        function.instruction_node_data =
            extract_instruction_node_data(entry.second.function.nodes_text);
        function.numeric_variables =
            extract_numeric_variables(entry.second.function.manifest_text);
        const auto profile_declarations = report.profiles.declarations.find(entry.first);
        if (profile_declarations != report.profiles.declarations.end()) {
            for (const auto& profile : profile_declarations->second) {
                const auto sidecar = profile.path.parent_path() / profile.id;
                auto text = read_text_file(sidecar);
                if (!text.empty()) {
                    function.profile_texts.emplace(profile.id, std::move(text));
                }
            }
        }

        for (const auto& payload_entry : entry.second.function.payload_blobs) {
            function.payloads.emplace(payload_entry.first, MigratedInstructionPayload{
                payload_entry.second.id,
                payload_entry.second.path,
                payload_entry.second.text});
        }

        result.package.functions.emplace(entry.first, std::move(function));
    }

    for (const auto& entry : report.linked_functions.functions) {
        apply_call_variable_overrides(
            result.package,
            entry.second.function.nodes_text);
    }

    return result;
}

std::string to_string(PackageEmissionDiagnosticCode code)
{
    switch (code) {
    case PackageEmissionDiagnosticCode::migration_report_has_errors:
        return "migration_report_has_errors";
    case PackageEmissionDiagnosticCode::retired_instruction_method:
        return "retired_instruction_method";
    }
    return "unknown";
}

} // namespace phoenix::migration
