#include "phoenix/migration/migrated_instruction_registry.hpp"

#include "phoenix/control/lod_instruction.hpp"
#include "phoenix/control/case_instruction.hpp"
#include "phoenix/control/choice_instruction.hpp"
#include "phoenix/control/if_instruction.hpp"
#include "phoenix/extrusion/instruction.hpp"
#include "phoenix/inset/instruction.hpp"
#include "phoenix/loop/instruction.hpp"
#include "phoenix/merge/instruction.hpp"
#include "phoenix/partition/instruction.hpp"
#include "phoenix/partition/ported/production/partition_solver_constraints.h"
#include "phoenix/partition/ported/production/partition_solver_filters.h"
#include "phoenix/rename/instruction.hpp"
#include "phoenix/scripting/variables.hpp"
#include "phoenix/select/instruction.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace phoenix::migration {

namespace {

struct InstructionKey {
    FunctionId function_id;
    NodeId node_id = 0;

    [[nodiscard]] bool operator<(const InstructionKey& other) const noexcept
    {
        if (function_id != other.function_id) return function_id < other.function_id;
        return node_id < other.node_id;
    }
};

template<typename T>
struct AdaptResult {
    std::optional<T> config;
    std::string reason;

    static AdaptResult adapted(T value)
    {
        return AdaptResult{std::move(value), {}};
    }

    static AdaptResult unsupported(std::string why)
    {
        return AdaptResult{std::nullopt, std::move(why)};
    }
};

std::string port_name_suffix(const PortId& port)
{
    const auto separator = port.find(':');
    return separator == std::string::npos ? port : port.substr(separator + 1);
}

std::optional<PortId> first_input_port(const InstructionDescriptor& instruction)
{
    if (instruction.input_ports.empty()) return std::nullopt;
    return instruction.input_ports.front().id;
}

std::optional<PortId> first_output_port(const InstructionDescriptor& instruction)
{
    if (instruction.output_ports.empty()) return std::nullopt;
    return instruction.output_ports.front().id;
}

std::optional<PortId> find_output_port_named(
    const InstructionDescriptor& instruction,
    const std::string& name)
{
    for (const auto& port : instruction.output_ports) {
        if (port_name_suffix(port.id) == name) return port.id;
    }
    return std::nullopt;
}

std::optional<PortId> find_input_port_named(
    const InstructionDescriptor& instruction,
    const std::string& name)
{
    for (const auto& port : instruction.input_ports) {
        if (port_name_suffix(port.id) == name) return port.id;
    }
    return std::nullopt;
}

std::optional<LabelId> label_id_for(
    const LoadedMigratedPackage& package,
    const std::string& uid);

int production_label_id_for(
    const LoadedMigratedPackage& package,
    const std::string& uid)
{
    if (uid.empty() || uid == "None") return -1;
    const auto label = label_id_for(package, uid);
    return label.has_value() ? static_cast<int>(label->value()) : -1;
}

std::vector<PortId> named_select_output_ports(const InstructionDescriptor& instruction)
{
    std::vector<PortId> ports;
    for (const auto& port : instruction.output_ports) {
        const auto suffix = port_name_suffix(port.id);
        if (suffix != "else" && suffix != "output") {
            ports.push_back(port.id);
        }
    }
    return ports;
}

std::string node_data_for(
    const MigratedFunctionPackage& function,
    NodeId node_id)
{
    const auto it = function.instruction_node_data.find(node_id);
    return it == function.instruction_node_data.end() ? std::string{} : it->second;
}

std::optional<std::string> json_string(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*"([^"]*)")regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    if (it == std::sregex_iterator{}) return std::nullopt;
    return (*it)[1].str();
}

std::optional<bool> json_bool(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*(true|false))regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    if (it == std::sregex_iterator{}) return std::nullopt;
    return (*it)[1].str() == "true";
}

std::optional<std::int64_t> json_int(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*(-?\d+))regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    if (it == std::sregex_iterator{}) return std::nullopt;
    return std::stoll((*it)[1].str());
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

std::string extract_object_text(const std::string& text, const std::string& key)
{
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return {};
    const auto colon = text.find(':', key_pos);
    if (colon == std::string::npos) return {};
    auto object_start = colon + 1;
    while (object_start < text.size()
           && std::isspace(static_cast<unsigned char>(text[object_start]))) {
        ++object_start;
    }
    if (object_start >= text.size() || text[object_start] != '{') return {};

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

bool json_array_has_values(const std::string& text, const std::string& key)
{
    const auto array = extract_array_text(text, key);
    for (const auto ch : array) {
        if (!std::isspace(static_cast<unsigned char>(ch))) return true;
    }
    return false;
}

std::vector<std::string> json_string_array(const std::string& text, const std::string& key)
{
    std::vector<std::string> result;
    const auto array = extract_array_text(text, key);
    const std::regex item{R"regex("([^"]*)")regex"};
    for (std::sregex_iterator it{array.begin(), array.end(), item};
         it != std::sregex_iterator{};
         ++it) {
        result.push_back((*it)[1].str());
    }
    return result;
}

std::vector<std::string> json_object_array(const std::string& text, const std::string& key)
{
    std::vector<std::string> result;
    const auto array = extract_array_text(text, key);
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t object_start = std::string::npos;
    for (std::size_t index = 0; index < array.size(); ++index) {
        const auto ch = array[index];
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
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && object_start != std::string::npos) {
                result.push_back(array.substr(object_start, index - object_start + 1));
                object_start = std::string::npos;
            }
        }
    }
    return result;
}

std::vector<std::string> json_array_items(const std::string& array)
{
    std::vector<std::string> result;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t item_start = 0;
    for (std::size_t index = 0; index < array.size(); ++index) {
        const auto ch = array[index];
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
        if (ch == '[' || ch == '{') ++depth;
        if (ch == ']' || ch == '}') --depth;
        if (ch == ',' && depth == 0) {
            result.push_back(array.substr(item_start, index - item_start));
            item_start = index + 1;
        }
    }
    if (item_start < array.size()) {
        result.push_back(array.substr(item_start));
    }
    return result;
}

std::optional<double> json_double(const std::string& text, const std::string& key)
{
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return std::nullopt;
    const auto colon = text.find(':', key_pos);
    if (colon == std::string::npos) return std::nullopt;
    auto start = colon + 1;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    auto end = start;
    if (end < text.size() && (text[end] == '+' || text[end] == '-')) ++end;
    bool saw_digit = false;
    while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
        saw_digit = true;
        ++end;
    }
    if (end < text.size() && text[end] == '.') {
        ++end;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
            saw_digit = true;
            ++end;
        }
    }
    if (!saw_digit) return std::nullopt;
    if (end < text.size() && (text[end] == 'e' || text[end] == 'E')) {
        const auto exponent = end;
        ++end;
        if (end < text.size() && (text[end] == '+' || text[end] == '-')) ++end;
        bool saw_exponent_digit = false;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
            saw_exponent_digit = true;
            ++end;
        }
        if (!saw_exponent_digit) end = exponent;
    }
    try {
        return std::stod(text.substr(start, end - start));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<extrusion::ProfileRef> production_profile_from_text(
    const LoadedMigratedPackage& package,
    const std::string& text)
{
    const auto segments_array = extract_array_text(text, "segments");
    std::vector<extrusion::ProfileSegment> segments;
    for (const auto& segment_array_text : json_array_items(segments_array)) {
        const auto first_bracket = segment_array_text.find('[');
        const auto last_bracket = segment_array_text.rfind(']');
        if (first_bracket == std::string::npos || last_bracket == std::string::npos
            || last_bracket <= first_bracket) {
            return std::nullopt;
        }
        const auto segment_inner = segment_array_text.substr(
            first_bracket + 1,
            last_bracket - first_bracket - 1);
        const auto items = json_array_items(segment_inner);
        if (items.size() < 3) return std::nullopt;
        const auto start_x = json_double(items[0], "x");
        const auto start_y = json_double(items[0], "y");
        const auto end_x = json_double(items[items.size() == 5 ? 3 : 1], "x");
        const auto end_y = json_double(items[items.size() == 5 ? 3 : 1], "y");
        if (!start_x.has_value() || !start_y.has_value()
            || !end_x.has_value() || !end_y.has_value()) {
            return std::nullopt;
        }
        const auto& data = items.back();
        extrusion::ProfileSegment segment;
        segment.delta_x = *end_x - *start_x;
        segment.delta_y = *end_y - *start_y;
        segment.horizontal = std::abs(segment.delta_y) < 1e-5;
        if (auto label = json_string(data, "label")) {
            segment.face_label = label_id_for(package, *label).value_or(unassigned_label_id);
        }
        if (auto label = json_string(data, "left_label")) {
            segment.left_label = label_id_for(package, *label).value_or(unassigned_label_id);
        }
        if (auto label = json_string(data, "bottom_label")) {
            segment.bottom_label = label_id_for(package, *label).value_or(unassigned_label_id);
        }
        if (auto label = json_string(data, "right_label")) {
            segment.right_label = label_id_for(package, *label).value_or(unassigned_label_id);
        }
        if (auto label = json_string(data, "top_label")) {
            segment.top_label = label_id_for(package, *label).value_or(unassigned_label_id);
        }
        if (auto label = json_string(data, "skirt_label")) {
            segment.skirt_label = label_id_for(package, *label).value_or(unassigned_label_id);
        }
        segments.push_back(segment);
    }
    if (segments.empty()) return std::nullopt;
    auto sign = CGAL::ZERO;
    for (const auto& segment : segments) {
        if (segment.delta_y > 0.0) {
            sign = CGAL::POSITIVE;
            break;
        }
        if (segment.delta_y < 0.0) {
            sign = CGAL::NEGATIVE;
            break;
        }
    }
    if (sign == CGAL::ZERO) return std::nullopt;
    auto profile = extrusion::Profile::create(std::move(segments), sign);
    if (!profile) return std::nullopt;
    return profile;
}

std::optional<std::string> single_mapped_profile_id(const std::string& node_data)
{
    const auto map_text = extract_array_text(node_data, "label_map");
    std::optional<std::string> profile_id;
    const std::regex item{R"regex(\{[^{}]*"to"\s*:\s*"([^"]*)"[^{}]*\})regex"};
    for (std::sregex_iterator it{map_text.begin(), map_text.end(), item};
         it != std::sregex_iterator{};
         ++it) {
        const auto current = (*it)[1].str();
        if (!profile_id.has_value()) {
            profile_id = current;
        } else if (*profile_id != current) {
            return std::nullopt;
        }
    }
    return profile_id;
}

const std::string* find_profile_text(
    const LoadedMigratedPackage& package,
    const MigratedFunctionPackage& function,
    const std::string& profile_id)
{
    const auto local = function.profile_texts.find(profile_id);
    if (local != function.profile_texts.end()) return &local->second;
    for (const auto& entry : package.functions) {
        const auto found = entry.second.profile_texts.find(profile_id);
        if (found != entry.second.profile_texts.end()) return &found->second;
    }
    return nullptr;
}

std::optional<double> json_number_or_quoted_numeric(
    const std::string& text,
    const std::string& key)
{
    if (auto value = json_double(text, key)) return value;
    const auto string_value = json_string(text, key);
    if (!string_value.has_value() || string_value->empty()) return std::nullopt;
    std::size_t consumed = 0;
    try {
        const auto value = std::stod(*string_value, &consumed);
        return consumed == string_value->size() ? std::optional<double>{value} : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
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
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            const auto exponent = pos_;
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            bool saw_exponent_digit = false;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                saw_exponent_digit = true;
                ++pos_;
            }
            if (!saw_exponent_digit) pos_ = exponent;
        }
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

std::optional<double> json_number_or_variable(
    const MigratedFunctionPackage& function,
    const std::string& text,
    const std::string& key)
{
    if (auto value = json_number_or_quoted_numeric(text, key)) return value;
    const auto string_value = json_string(text, key);
    if (!string_value.has_value()) return std::nullopt;
    return NumericExpressionParser{*string_value, function.numeric_variables}.parse();
}

std::optional<LabelId> label_id_for(
    const LoadedMigratedPackage& package,
    const std::string& uid)
{
    if (uid.empty()) return std::nullopt;
    const auto it = package.label_ids.find(uid);
    if (it == package.label_ids.end()) return std::nullopt;
    return LabelId{it->second};
}

bool set_optional_label(
    const LoadedMigratedPackage& package,
    const std::string& node_data,
    const std::string& key,
    std::optional<LabelId>& target)
{
    const auto uid = json_string(node_data, key).value_or("");
    if (uid.empty()) return true;
    target = label_id_for(package, uid);
    return target.has_value();
}

std::optional<LabelId> required_label(
    const LoadedMigratedPackage& package,
    const std::string& node_data,
    const std::string& key)
{
    const auto uid = json_string(node_data, key).value_or("");
    if (uid.empty()) return std::nullopt;
    return label_id_for(package, uid);
}

rename::LengthKind rename_length_kind(const std::string& value)
{
    if (value == "largest" || value == "the largest") return rename::LengthKind::largest;
    if (value == "smallest" || value == "the smallest") return rename::LengthKind::smallest;
    if (value == "any") return rename::LengthKind::any;
    return rename::LengthKind::none;
}

rename::AdjacentRelation rename_adjacent_relation(const std::string& value)
{
    if (value == "previous") return rename::AdjacentRelation::previous;
    if (value == "next") return rename::AdjacentRelation::next;
    return rename::AdjacentRelation::any;
}

scripting::ExpressionSpec migrated_expression(
    std::string source,
    const MigratedFunctionPackage& function)
{
    scripting::ExpressionSpec spec;
    spec.program.source = std::move(source);
    for (const auto& variable : function.numeric_variables) {
        spec.global_bindings[variable.first] = variable.second;
    }
    return spec;
}

scripting::VariablePlan migrated_variable_plan(
    const MigratedFunctionPackage& function,
    const std::string& node_data)
{
    scripting::VariablePlan plan;
    for (const auto& variable : function.numeric_variables) {
        plan.parent_bindings[variable.first] = variable.second;
    }
    for (const auto& variable_data : json_object_array(node_data, "variables")) {
        scripting::VariableSpec spec;
        spec.name = json_string(variable_data, "name").value_or("");
        if (auto initial = json_number_or_variable(function, variable_data, "initialValue")) {
            spec.initial_value = *initial;
        }
        if (auto expression = json_string(variable_data, "expression");
            expression.has_value() && !expression->empty()) {
            spec.update = migrated_expression(std::move(*expression), function);
        }
        plan.variables.push_back(std::move(spec));
    }
    return plan;
}

scripting::BindingRelation migrated_binding_relation(const std::string& relation)
{
    if (relation == "previous") return scripting::BindingRelation::previous;
    if (relation == "next") return scripting::BindingRelation::next;
    return scripting::BindingRelation::any;
}

bool append_geometry_binding(
    const LoadedMigratedPackage& package,
    const std::string& binding_data,
    scripting::GeometryBindingPlan& plan)
{
    const auto type = json_string(binding_data, "typeId").value_or("");
    const auto variable = json_string(binding_data, "variable").value_or("");
    if (variable.empty()) return false;

    scripting::GeometryBindingSpec spec;
    spec.variable = variable;
    if (type == "countLabelsBinding") {
        spec.kind = scripting::GeometryBindingKind::count_edge_labels;
        plan.element_kind = scripting::BindingElementKind::face;
        if (!set_optional_label(package, binding_data, "label", spec.label1)) return false;
    } else if (type == "countFaceLabelsBinding") {
        spec.kind = scripting::GeometryBindingKind::count_face_labels;
        plan.element_kind = scripting::BindingElementKind::face;
        if (!set_optional_label(package, binding_data, "label", spec.label1)) return false;
    } else if (type == "lengthBinding") {
        spec.kind = scripting::GeometryBindingKind::length;
        plan.element_kind = scripting::BindingElementKind::face;
        if (!set_optional_label(package, binding_data, "label", spec.label1)) return false;
        const auto kind = json_string(binding_data, "kind").value_or("");
        if (kind == "the largest" || kind == "largest") {
            spec.choice = scripting::BindingChoice::largest;
        } else if (kind == "the smallest" || kind == "smallest") {
            spec.choice = scripting::BindingChoice::shortest;
        }
    } else {
        return false;
    }
    spec.relation = migrated_binding_relation(json_string(binding_data, "relation").value_or(""));
    plan.bindings.push_back(std::move(spec));
    return true;
}

std::optional<scripting::GeometryBindingPlan> geometry_bindings_for(
    const LoadedMigratedPackage& package,
    const std::string& node_data)
{
    scripting::GeometryBindingPlan plan;
    for (const auto& binding_data : json_object_array(node_data, "bindings")) {
        if (!append_geometry_binding(package, binding_data, plan)) return std::nullopt;
    }
    return plan.bindings.empty()
        ? std::nullopt
        : std::optional<scripting::GeometryBindingPlan>{std::move(plan)};
}

std::optional<control::IfInstructionConfig> adapt_if(
    const LoadedMigratedPackage& package,
    const MigratedFunctionPackage& function,
    const InstructionDescriptor& instruction,
    const std::string& node_data)
{
    auto input = first_input_port(instruction);
    auto then_port = find_output_port_named(instruction, "then")
        .value_or(find_output_port_named(instruction, "true").value_or(PortId{}));
    auto else_port = find_output_port_named(instruction, "else")
        .value_or(find_output_port_named(instruction, "false").value_or(PortId{}));
    if (!input.has_value() || then_port.empty() || else_port.empty()) return std::nullopt;

    control::IfInstructionConfig config;
    config.input_port = *input;
    config.then_port = then_port;
    config.else_port = else_port;

    const auto method = json_string(node_data, "method").value_or("variable");
    if (method == "expression") {
        auto expression = json_string(node_data, "expression").value_or("");
        if (expression.empty()) return std::nullopt;
        config.expression = migrated_expression(std::move(expression), function);
    } else {
        auto variable = json_string(node_data, "variable").value_or("");
        if (variable.empty()) return std::nullopt;
        config.expression = migrated_expression(std::move(variable), function);
    }
    config.geometry_bindings = geometry_bindings_for(package, node_data);
    if (!config.geometry_bindings && json_array_has_values(node_data, "bindings")) return std::nullopt;
    return config;
}

std::optional<control::CaseInstructionConfig> adapt_case(
    const LoadedMigratedPackage& package,
    const MigratedFunctionPackage& function,
    const InstructionDescriptor& instruction,
    const std::string& node_data)
{
    auto input = first_input_port(instruction);
    if (!input.has_value()) return std::nullopt;
    control::CaseInstructionConfig config;
    config.input_port = *input;
    config.else_port = find_output_port_named(instruction, "else")
        .value_or(find_output_port_named(instruction, "output").value_or(PortId{"else"}));

    for (const auto& expression_data : json_object_array(node_data, "expressions")) {
        const auto name = json_string(expression_data, "name").value_or("");
        const auto expression = json_string(expression_data, "expression").value_or("");
        if (name.empty() || expression.empty()) continue;
        auto port = find_output_port_named(instruction, name);
        if (!port.has_value()) return std::nullopt;
        config.branches.push_back(control::CaseBranch{
            *port,
            migrated_expression(expression, function)});
    }
    if (config.branches.empty()) return std::nullopt;
    config.geometry_bindings = geometry_bindings_for(package, node_data);
    if (!config.geometry_bindings && json_array_has_values(node_data, "bindings")) return std::nullopt;
    return config;
}

std::optional<control::ChoiceInstructionConfig> adapt_choice(
    const InstructionDescriptor& instruction,
    const std::string& node_data)
{
    auto input = first_input_port(instruction);
    if (!input.has_value()) return std::nullopt;

    control::ChoiceInstructionConfig config;
    config.input_port = *input;
    config.published_name = json_string(node_data, "publishedName").value_or("");
    config.published_group = json_string(node_data, "publishedGroup").value_or("");
    config.always_visible = json_bool(node_data, "alwaysVisible").value_or(false);
    config.important = json_bool(node_data, "important").value_or(false);

    for (const auto& choice : json_string_array(node_data, "choices")) {
        auto port = find_output_port_named(instruction, choice);
        if (!port.has_value()) return std::nullopt;
        config.items.push_back(control::ChoiceItem{*port, {}, {}});
        if (!config.selected_output) config.selected_output = *port;
    }
    return config.items.empty() ? std::nullopt : std::optional<control::ChoiceInstructionConfig>{config};
}

InstructionHandler make_unsupported_handler(std::string kind)
{
    return [kind = std::move(kind)](const InstructionExecutionFrame& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        std::ostringstream stream;
        stream << "Migrated instruction kind '" << kind
               << "' has no payload adapter/runtime handler yet.";
        result.failure_message = stream.str();
        return result;
    };
}

std::optional<control::LodInstructionConfig> adapt_lod(
    const InstructionDescriptor& instruction)
{
    auto input = first_input_port(instruction);
    auto low = find_output_port_named(instruction, "low");
    auto normal = find_output_port_named(instruction, "normal");
    auto high = find_output_port_named(instruction, "high");
    if (!input.has_value() || !low.has_value() || !normal.has_value() || !high.has_value()) {
        return std::nullopt;
    }

    control::LodInstructionConfig config;
    config.input_port = *input;
    config.low_port = *low;
    config.normal_port = *normal;
    config.high_port = *high;
    return config;
}

std::optional<merge::InstructionConfig> adapt_merge(
    const InstructionDescriptor& instruction,
    const std::string& node_data)
{
    auto input = first_input_port(instruction);
    auto output = first_output_port(instruction);
    if (!input.has_value() || !output.has_value()) return std::nullopt;

    const auto method = json_string(node_data, "method").value_or("");
    const bool method_edges = method == "edges";

    merge::InstructionConfig config;
    config.geometry_input_port = *input;
    config.geometry_output_port = *output;
    config.options.join_vertices = json_bool(node_data, "joinVertexs").value_or(method_edges);
    config.options.merge_faces = json_bool(node_data, "mergeFaces").value_or(false);
    config.options.merge_faces_labels = json_bool(node_data, "mergeFacesLabels").value_or(false);
    config.options.join_collinear = json_bool(node_data, "joinColineal").value_or(false);
    config.options.merge_borders = json_bool(node_data, "mergeBorders").value_or(method_edges);
    return config;
}

AdaptResult<extrusion::InstructionConfig> adapt_extrusion(
    const LoadedMigratedPackage& package,
    const MigratedFunctionPackage& function,
    const InstructionDescriptor& instruction,
    const std::string& node_data)
{
    auto input = first_input_port(instruction);
    auto output = first_output_port(instruction);
    if (!input.has_value() || !output.has_value()) {
        return AdaptResult<extrusion::InstructionConfig>::unsupported("missing_geometry_port");
    }
    if (node_data.empty()) {
        return AdaptResult<extrusion::InstructionConfig>::unsupported("missing_node_data");
    }

    auto method = json_string(node_data, "method").value_or("amount");
    const auto profile_object = extract_object_text(node_data, "profile");
    if (method == "amount" && !profile_object.empty()) {
        method = "profile";
    }
    const auto amount = json_number_or_variable(function, node_data, "amount");
    if (method == "label") {
        return AdaptResult<extrusion::InstructionConfig>::unsupported("retired_label_method");
    }
    if (method != "map" && method != "profile" && !amount.has_value()) {
        return AdaptResult<extrusion::InstructionConfig>::unsupported(
            "unresolved_amount:" + method + ":" + json_string(node_data, "amount").value_or("<non-string>"));
    }
    if (method != "map" && method != "profile" && *amount == 0.0) {
        return AdaptResult<extrusion::InstructionConfig>::unsupported("zero_amount:" + method);
    }
    if (json_double(node_data, "range").value_or(-1.0) >= 0.0
        || json_double(node_data, "step").value_or(-1.0) >= 0.0) {
        return AdaptResult<extrusion::InstructionConfig>::unsupported("range_or_step");
    }

    extrusion::InstructionConfig config;
    config.geometry_input_port = *input;
    config.geometry_output_port = *output;
    const auto bottom = label_id_for(package, "00000000-0000-0000-0000-000000000004");
    const auto right = label_id_for(package, "00000000-0000-0000-0000-000000000001");
    const auto top = label_id_for(package, "00000000-0000-0000-0000-000000000002");
    const auto left = label_id_for(package, "00000000-0000-0000-0000-000000000003");
    if (!bottom.has_value() || !right.has_value() || !top.has_value() || !left.has_value()) {
        return AdaptResult<extrusion::InstructionConfig>::unsupported("missing_system_label");
    }
    config.bottom_label = *bottom;
    config.right_label = *right;
    config.top_label = *top;
    config.left_label = *left;

    if (auto cap = json_string(node_data, "cap")) {
        config.cap_label = label_id_for(package, *cap).value_or(unassigned_label_id);
    }
    if (auto skirt = json_string(node_data, "skirt")) {
        config.skirt_label = label_id_for(package, *skirt).value_or(unassigned_label_id);
    }
    if (method == "profile") {
        const auto profile_id = !profile_object.empty()
            ? json_string(profile_object, "id")
            : json_string(node_data, "profile");
        if (!profile_id.has_value()) {
            return AdaptResult<extrusion::InstructionConfig>::unsupported("profile_missing_id");
        }
        const auto* profile_text = find_profile_text(package, function, *profile_id);
        if (profile_text == nullptr) {
            return AdaptResult<extrusion::InstructionConfig>::unsupported("profile_missing_profile");
        }
        const auto profile = production_profile_from_text(package, *profile_text);
        if (!profile.has_value()) {
            return AdaptResult<extrusion::InstructionConfig>::unsupported("profile_unreadable_profile");
        }
        config.profile = *profile;
    } else if (method == "map") {
        const auto profile_id = single_mapped_profile_id(node_data);
        if (!profile_id.has_value()) {
            return AdaptResult<extrusion::InstructionConfig>::unsupported("map_multiple_profiles");
        }
        const auto* profile_text = find_profile_text(package, function, *profile_id);
        if (profile_text == nullptr) {
            return AdaptResult<extrusion::InstructionConfig>::unsupported("map_missing_profile");
        }
        const auto profile = production_profile_from_text(package, *profile_text);
        if (!profile.has_value()) {
            return AdaptResult<extrusion::InstructionConfig>::unsupported("map_unreadable_profile");
        }
        config.profile = *profile;
    } else {
        const auto sides = json_string(node_data, "sides");
        const auto side_label = sides ? label_id_for(package, *sides) : std::nullopt;
        extrusion::ProfileSegment segment;
        segment.delta_y = *amount;
        segment.face_label = side_label.value_or(unassigned_label_id);
        segment.left_label = side_label.value_or(config.left_label);
        segment.bottom_label = side_label.value_or(config.bottom_label);
        segment.right_label = side_label.value_or(config.right_label);
        segment.top_label = side_label.value_or(config.top_label);
        segment.skirt_label = config.skirt_label;
        config.profile = extrusion::Profile::create(
            std::vector<extrusion::ProfileSegment>{segment},
            *amount < 0.0 ? CGAL::NEGATIVE : CGAL::POSITIVE);
    }
    if (!config.profile) {
        return AdaptResult<extrusion::InstructionConfig>::unsupported("invalid_profile");
    }
    return AdaptResult<extrusion::InstructionConfig>::adapted(std::move(config));
}

struct PartitionLoadContext {
    const LoadedMigratedPackage& package;
    std::vector<std::shared_ptr<partition_cut>> cuts;
    std::map<std::string, cut_segment_id> segments;
    std::map<std::string, int> cut_indexes;
    int base_segment_count = 0;
    std::string unsupported_reason;

    cut_segment_id add_base_segment(const std::string& id)
    {
        const auto existing = segments.find(id);
        if (existing != segments.end()) return existing->second;
        const cut_segment_id result{static_cast<int>(segments.size())};
        segments[id] = result;
        return result;
    }

    partition_cut* cut(int index) const
    {
        return index >= 0 && static_cast<std::size_t>(index) < cuts.size()
            ? cuts[static_cast<std::size_t>(index)].get()
            : nullptr;
    }

    partition_cut* cut_from_id(const std::string& id) const
    {
        const auto found = cut_indexes.find(id);
        return found == cut_indexes.end() ? nullptr : cut(found->second);
    }

    cut_segment_id segment(const std::string& id) const
    {
        const auto found = segments.find(id);
        return found == segments.end() ? cut_segment_id{} : found->second;
    }

    int add_cut(const std::string& id, partition_cut& cut)
    {
        const auto index = static_cast<int>(cuts.size());
        cut.segment = cut_segment_id{base_segment_count + index * 5};
        segments[id] = cut.segment;
        segments[id + "Opp"] = cut.segment;
        cut_indexes[id] = index;
        return index;
    }

    void add_cut_result(const std::string& id, const std::string& kind, const partition_cut& cut)
    {
        if (kind == "SourceL") segments[id] = cut.segment.cut_result(SOURCE_LEFT);
        else if (kind == "SourceR") segments[id] = cut.segment.cut_result(SOURCE_RIGHT);
        else if (kind == "TargetL") segments[id] = cut.segment.cut_result(TARGET_LEFT);
        else if (kind == "TargetR") segments[id] = cut.segment.cut_result(TARGET_RIGHT);
    }

    bool is_base_segment(cut_segment_id id) const
    {
        return id.value >= 0 && id.value < base_segment_count;
    }

    bool is_cut_segment(cut_segment_id id, int& cut_id, cut_segment_type& type) const
    {
        type = BASE_SEGMENT;
        cut_id = -1;
        const auto cut_space = id.value - base_segment_count;
        if (cut_space < 0) return false;
        cut_id = cut_space / 5;
        type.set(cut_space % 5);
        return true;
    }

    vm::variable_value value(const boost::property_tree::ptree& data, const std::string& key, double fallback = 0.0) const
    {
        if (const auto text = data.get_optional<std::string>(key)) {
            if (!text->empty()) return vm::variable_value{*text, fallback};
        }
        if (const auto number = data.get_optional<double>(key)) {
            return vm::variable_value{*number};
        }
        return vm::variable_value{};
    }

    int label(const std::string& uid) const
    {
        return production_label_id_for(package, uid);
    }
};

std::optional<boost::property_tree::ptree> parse_json_tree(const std::string& text)
{
    try {
        std::istringstream input{text};
        boost::property_tree::ptree tree;
        boost::property_tree::read_json(input, tree);
        return tree;
    } catch (...) {
        return std::nullopt;
    }
}

bool partition_segment_is_bezier(const boost::property_tree::ptree& segment)
{
    return segment.size() >= 5;
}

const boost::property_tree::ptree* partition_segment_data(
    const boost::property_tree::ptree& segment)
{
    if (segment.empty()) return nullptr;
    return &segment.back().second;
}

void load_partition_base_segments(
    const boost::property_tree::ptree& base_curve_iteration,
    PartitionLoadContext& context)
{
    for (const auto& curve_entry : base_curve_iteration) {
        const auto& curve = curve_entry.second;
        if (const auto segments = curve.get_child_optional("segments")) {
            for (const auto& segment_entry : *segments) {
                const auto* data = partition_segment_data(segment_entry.second);
                if (!data) continue;
                if (data->get<std::string>("Type", {}) == "BaseSegment") {
                    context.add_base_segment(data->get<std::string>("Id", {}));
                }
            }
        }
    }
}

std::shared_ptr<partition_cut> load_partition_cut(
    const boost::property_tree::ptree& data,
    PartitionLoadContext& context)
{
    partition_cut* parent = nullptr;
    if (const auto parent_index = data.get_optional<int>("ParentIdx")) {
        parent = context.cut(*parent_index);
    }
    auto cut = std::make_shared<partition_cut>(parent);
    if (parent) {
        if (data.get<bool>("Left", false)) parent->left = cut.get();
        else parent->right = cut.get();
    }
    const auto cut_id = data.get<std::string>("CutSegment", {});
    cut->id = context.add_cut(cut_id, *cut);
    cut->src = context.segment(data.get<std::string>("Source", {}));
    cut->dst = context.segment(data.get<std::string>("Target", {}));
    context.add_cut_result(data.get<std::string>("SourceR", {}), "SourceR", *cut);
    context.add_cut_result(data.get<std::string>("SourceL", {}), "SourceL", *cut);
    context.add_cut_result(data.get<std::string>("TargetR", {}), "TargetR", *cut);
    context.add_cut_result(data.get<std::string>("TargetL", {}), "TargetL", *cut);
    cut->control_points.load(const_cast<boost::property_tree::ptree&>(data));
    context.cuts.push_back(cut);
    return cut;
}

int partition_label_from_child(
    const boost::property_tree::ptree& data,
    const std::string& key,
    const PartitionLoadContext& context)
{
    return context.label(data.get<std::string>(key, {}));
}

void load_partition_repeat(
    const boost::property_tree::ptree& data,
    partition_cut& cut,
    const PartitionLoadContext& context)
{
    const auto repeat_child = data.get_child_optional("repeat");
    if (!repeat_child) return;
    const auto& repeat = *repeat_child;
    cut.repeat.direction = CUT_DIR_INTERPOLATE;
    const auto direction = repeat.get<std::string>("direction", "dirInterpolate");
    if (direction == "dirParallelSource") cut.repeat.direction = CUT_DIR_SOURCE;
    else if (direction == "dirParallelTarget") cut.repeat.direction = CUT_DIR_TARGET;
    else if (direction == "dirPerpendicularCut") cut.repeat.direction = CUT_DIR_CUT;

    cut.repeat.secondary = context.value(repeat, "margin", 0.0);
    cut.repeat.secondary_range = context.value(repeat, "marginRange", -1.0);
    cut.repeat.secondary_step = context.value(repeat, "marginStep", -1.0);
    if (repeat.get<bool>("nTimes", false)) {
        cut.repeat.kind = REPEAT_N_TIMES;
        cut.repeat.count = context.value(repeat, "times", 0.0);
        cut.repeat.count_range = context.value(repeat, "timesRange", -1.0);
        cut.repeat.count_step = context.value(repeat, "timesStep", -1.0);
    } else if (repeat.get<bool>("byLength", false)) {
        cut.repeat.kind = REPEAT_BY_LENGTH;
        cut.repeat.length = context.value(repeat, "length", 0.0);
        cut.repeat.length_range = context.value(repeat, "lengthRange", -1.0);
        cut.repeat.length_step = context.value(repeat, "lengthStep", -1.0);
        cut.repeat.maximun_cuts = context.value(repeat, "lengthTimes", 0.0);
        cut.repeat.maximun_cuts_range = context.value(repeat, "lengthTimesRange", -1.0);
    }
    const auto adjust = repeat.get<std::string>("adjustMode", {});
    if (adjust == "adjSecondary") cut.repeat.adjust_mode = CUT_ADJ_SECONDARY;
    else if (adjust == "adjExtremes") cut.repeat.adjust_mode = CUT_ADJ_EXTREMES;
    else if (adjust == "adjFirst") cut.repeat.adjust_mode = CUT_ADJ_FIRST;
    else if (adjust == "adjLast") cut.repeat.adjust_mode = CUT_ADJ_LAST;
    else cut.repeat.adjust_mode = CUT_ADJ_PRIMARY;

    const auto extent = repeat.get<std::string>("cutExtent", "basic");
    if (extent == "source") cut.repeat.extent = CUT_EXT_SOURCE;
    else if (extent == "target") cut.repeat.extent = CUT_EXT_TARGET;
    else if (extent == "source_target") cut.repeat.extent = CUT_EXT_SOURCE_TARGET;
    else cut.repeat.extent = CUT_EXT_BASIC;

    auto get_label = [&repeat, &context](const std::string& key, int fallback = -1) {
        const auto value = repeat.get<std::string>(key, {});
        return value.empty() ? fallback : context.label(value);
    };
    cut.repeat.faceLabel = get_label("faceLabel");
    cut.repeat.secondaryLabel = get_label("spaceLabel");
    const auto margin_label = get_label("marginLabel");
    cut.repeat.marginStartFaceLabel = get_label("marginStartFaceLabel", margin_label);
    cut.repeat.marginEndFaceLabel = get_label("marginEndFaceLabel", margin_label);
    if (cut.repeat.marginStartFaceLabel < 0) cut.repeat.marginStartFaceLabel = cut.repeat.secondaryLabel;
    if (cut.repeat.marginEndFaceLabel < 0) cut.repeat.marginEndFaceLabel = cut.repeat.secondaryLabel;
    cut.repeat.primaryEdgeLabel = get_label("faceEdgeLabel");
    cut.repeat.secondaryEdgeLabel = get_label("spaceEdgeLabel");
    const auto margin_edge_label = get_label("marginEdgeLabel");
    cut.repeat.marginStartEdgeLabel = get_label("marginStartEdgeLabel", margin_edge_label);
    cut.repeat.marginEndEdgeLabel = get_label("marginEndEdgeLabel", margin_edge_label);
    cut.repeat.faceBottomLabel = get_label("faceBottomLabel");
    cut.repeat.faceTopLabel = get_label("faceTopLabel");
    cut.repeat.faceRightLabel = get_label("faceRightLabel");
    cut.repeat.faceLeftLabel = get_label("faceLeftLabel");
    cut.repeat.faceRightLabelOpp = get_label("faceRightLabelOpp");
    cut.repeat.faceLeftLabelOpp = get_label("faceLeftLabelOpp");
    cut.repeat.secondaryBottomLabel = get_label("spaceBottomLabel");
    cut.repeat.secondaryTopLabel = get_label("spaceTopLabel");
    cut.repeat.secondaryRightLabel = get_label("spaceRightLabel");
    cut.repeat.secondaryLeftLabel = get_label("spaceLeftLabel");
    cut.repeat.secondaryRightLabelOpp = get_label("spaceRightLabelOpp");
    cut.repeat.secondaryLeftLabelOpp = get_label("spaceLeftLabelOpp");
}

void load_partition_cut_labels(
    const boost::property_tree::ptree& data,
    partition_cut& cut,
    const PartitionLoadContext& context)
{
    cut.faceLeft = partition_label_from_child(data, "FaceLeft", context);
    cut.faceRight = partition_label_from_child(data, "FaceRight", context);
    cut.cutLeft = partition_label_from_child(data, "CutLeft", context);
    cut.cutRight = partition_label_from_child(data, "CutRight", context);
    load_partition_repeat(data, cut, context);
}

bool add_partition_distance_constraint(
    PartitionLoadContext& context,
    partition_model& model,
    const std::string& owner,
    const boost::property_tree::ptree& data,
    bool has_min,
    vm::variable_value min,
    bool has_max,
    vm::variable_value max)
{
    const auto reference = data.get<std::string>("reference", {});
    const auto segment = context.segment(owner);
    const auto ref = context.segment(reference);
    int cut_id = -1;
    cut_segment_type type;
    context.is_cut_segment(segment, cut_id, type);
    if (segment.empty() || ref.empty()) return false;
    model.add_constraint(std::make_shared<segment_distance_constraint>(
        model.constraint_index(),
        cut_id,
        segment,
        ref,
        has_min,
        min,
        has_max,
        max));
    return true;
}

bool load_partition_condition(
    PartitionLoadContext& context,
    partition_model& model,
    const boost::property_tree::ptree& condition)
{
    const auto type = condition.get<std::string>("typeId", {});
    const auto data_child = condition.get_child_optional("data");
    if (!data_child) return false;
    const auto& data = *data_child;
    const auto owner = data.get<std::string>("ownerId", {});
    const auto segment = context.segment(owner);
    if (type == "willBeLabeled") {
        const auto label = context.label(data.get<std::string>("label", {}));
        if (auto* cut = context.cut_from_id(owner)) {
            cut->cutRight = label;
            return true;
        }
        if (context.is_base_segment(segment)) {
            model.add_base_label(segment, label);
            return true;
        }
        return true;
    }
    if (type == "isLabeled") {
        const auto label = context.label(data.get<std::string>("label", {}));
        if (context.is_base_segment(segment)) {
            model.add_base_label(segment, label);
            return true;
        }
        return true;
    }
    if (type == "exactDistance") {
        const auto value = context.value(data, "value", 0.0);
        return add_partition_distance_constraint(context, model, owner, data, true, value, true, value);
    }
    if (type == "fartherThan") {
        return add_partition_distance_constraint(
            context, model, owner, data, true, context.value(data, "value", 0.0), false, {});
    }
    if (type == "parallel" || type == "perpendicular" || type == "exactAngle") {
        const auto reference = data.get<std::string>("reference", {});
        const auto ref = context.segment(reference);
        if (segment.empty() || ref.empty()) return false;
        const auto angle = type == "perpendicular"
            ? vm::variable_value{90.0}
            : (type == "exactAngle" ? context.value(data, "value", 0.0) : vm::variable_value{0.0});
        if (context.is_base_segment(segment) && context.is_base_segment(ref)) {
            base_angle_filter::add_to_model(model, segment, ref, angle, angle);
        } else {
            model.add_constraint(std::make_shared<segments_angle_constraint>(
                model.constraint_index(), segment, ref, angle, angle));
        }
        return true;
    }
    if (type == "exactLength" || type == "exactLengthPct") {
        const auto value = context.value(data, "value", 0.0);
        if (segment.empty()) return false;
        if (type == "exactLengthPct") {
            const auto reference = context.segment(data.get<std::string>("referencePct", {}));
            model.add_constraint(std::make_shared<segment_length_constraint_pct>(
                model.constraint_index(), segment, value, value, reference));
        } else {
            model.add_constraint(std::make_shared<segment_length_constraint>(
                model.constraint_index(), segment, value, value));
        }
        return true;
    }
    if (type == "randomizeSource" || type == "randomizeTarget") {
        if (auto* cut = context.cut_from_id(owner)) {
            if (type == "randomizeSource") cut->randomize_source = true;
            else cut->randomize_target = true;
        }
        return true;
    }
    context.unsupported_reason = "condition_" + type;
    return false;
}

AdaptResult<partition::InstructionConfig> adapt_partition(
    const LoadedMigratedPackage& package,
    const MigratedFunctionPackage& function,
    const InstructionDescriptor& instruction,
    const std::string& node_data)
{
    auto input = first_input_port(instruction);
    auto output = first_output_port(instruction);
    if (!input.has_value() || !output.has_value()) {
        return AdaptResult<partition::InstructionConfig>::unsupported("ports");
    }
    const auto file_id = json_string(node_data, "file");
    if (!file_id.has_value() || file_id->empty()) {
        return AdaptResult<partition::InstructionConfig>::unsupported("missing_file");
    }
    const auto payload = function.payloads.find(*file_id);
    if (payload == function.payloads.end()) {
        return AdaptResult<partition::InstructionConfig>::unsupported("missing_payload");
    }
    const auto tree = parse_json_tree(payload->second.text);
    if (!tree.has_value()) {
        return AdaptResult<partition::InstructionConfig>::unsupported("invalid_payload_json");
    }

    PartitionLoadContext context{package};
    const auto base_curves = tree->get_child_optional("baseCurves");
    if (!base_curves || base_curves->empty()) {
        return AdaptResult<partition::InstructionConfig>::unsupported("missing_base_curves");
    }
    load_partition_base_segments(base_curves->begin()->second, context);
    context.base_segment_count = static_cast<int>(context.segments.size());

    partition_cut* root = nullptr;
    const auto cut_helpers = tree->get_child_optional("cutHelpers");
    if (!cut_helpers || cut_helpers->empty()) {
        return AdaptResult<partition::InstructionConfig>::unsupported("missing_cut_helpers");
    }
    for (const auto& cut_entry : *cut_helpers) {
        auto cut = load_partition_cut(cut_entry.second, context);
        if (!cut->parent) root = cut.get();
    }
    if (!root) return AdaptResult<partition::InstructionConfig>::unsupported("missing_root_cut");

    for (const auto& iteration_entry : *base_curves) {
        for (const auto& curve_entry : iteration_entry.second) {
            if (const auto segments = curve_entry.second.get_child_optional("segments")) {
                for (const auto& segment_entry : *segments) {
                    const auto* data = partition_segment_data(segment_entry.second);
                    if (!data || data->get<std::string>("Type", {}) != "CutSegment") continue;
                    if (auto* cut = context.cut_from_id(data->get<std::string>("Id", {}))) {
                        cut->control_points.enabled =
                            cut->control_points.enabled && partition_segment_is_bezier(segment_entry.second);
                        load_partition_cut_labels(*data, *cut, context);
                    }
                }
            }
        }
    }

    auto prototype = std::make_shared<PartitionLoadContext>(std::move(context));
    auto build_model = [prototype]() {
        std::map<partition_cut*, partition_cut*> remap;
        std::vector<std::shared_ptr<partition_cut>> cloned_cuts;
        cloned_cuts.reserve(prototype->cuts.size());
        for (const auto& original : prototype->cuts) {
            auto cloned = std::make_shared<partition_cut>(nullptr);
            *cloned = *original;
            cloned->parent = nullptr;
            cloned->left = nullptr;
            cloned->right = nullptr;
            remap[original.get()] = cloned.get();
            cloned_cuts.push_back(cloned);
        }
        partition_cut* root_clone = nullptr;
        for (std::size_t index = 0; index < prototype->cuts.size(); ++index) {
            auto* original = prototype->cuts[index].get();
            auto* cloned = cloned_cuts[index].get();
            cloned->parent = original->parent ? remap[original->parent] : nullptr;
            cloned->left = original->left ? remap[original->left] : nullptr;
            cloned->right = original->right ? remap[original->right] : nullptr;
            if (!cloned->parent) root_clone = cloned;
        }
        return partition::ProductionModelRef{
            new partition_model(root_clone, prototype->base_segment_count),
            [cloned_cuts = std::move(cloned_cuts)](partition_model* model) mutable {
                delete model;
                cloned_cuts.clear();
            }};
    };

    auto initial_model = build_model();
    if (!initial_model) {
        return AdaptResult<partition::InstructionConfig>::unsupported("model_build");
    }
    if (const auto conditions = tree->get_child_optional("conditions")) {
        for (const auto& condition_entry : *conditions) {
            if (!load_partition_condition(*prototype, *initial_model, condition_entry.second)) {
                return AdaptResult<partition::InstructionConfig>::unsupported(
                    prototype->unsupported_reason.empty() ? "condition" : prototype->unsupported_reason);
            }
        }
    }
    const auto prototype_constraints = initial_model->constraints();
    const auto prototype_filters = initial_model->filters;
    const auto prototype_base_labels = initial_model->get_base_labels();

    partition::InstructionConfig config;
    config.geometry_input_port = *input;
    config.geometry_output_port = *output;
    config.values = function.numeric_variables;
    config.model_factory = [prototype,
                            build_model,
                            prototype_constraints,
                            prototype_filters,
                            prototype_base_labels]() mutable {
        auto model = build_model();
        if (!model) return model;
        for (const auto& constraint : prototype_constraints) {
            model->add_constraint(constraint);
        }
        model->filters = prototype_filters;
        for (const auto& label : prototype_base_labels) {
            if (label.second.label >= 0) {
                model->add_base_label(label.first, label.second.label);
            }
            if (label.second.opp_label >= 0) {
                model->add_base_opp_label(label.first, label.second.opp_label);
            }
        }
        return model;
    };
    const auto conditions = tree->get_child_optional("conditions");
    if (conditions) {
        for (const auto& condition_entry : *conditions) {
            const auto type = condition_entry.second.get<std::string>("typeId", {});
            if (type != "isLabeled") continue;
            const auto data = condition_entry.second.get_child_optional("data");
            if (!data) continue;
            const auto segment = prototype->segment(data->get<std::string>("ownerId", {}));
            if (!prototype->is_base_segment(segment)) continue;
            const auto label = production_label_id_for(package, data->get<std::string>("label", {}));
            if (label >= 0) config.base_segment_labels[segment.value] = static_cast<LabelId>(label);
        }
    }
    for (const auto& label : prototype_base_labels) {
        if (label.second.label >= 0) {
            config.base_segment_labels[label.first.value] = static_cast<LabelId>(label.second.label);
        }
    }
    return AdaptResult<partition::InstructionConfig>::adapted(std::move(config));
}

std::optional<rename::InstructionConfig> adapt_rename(
    const LoadedMigratedPackage& package,
    const InstructionDescriptor& instruction,
    const std::string& node_data)
{
    auto input = first_input_port(instruction);
    auto output = first_output_port(instruction);
    if (!input.has_value() || !output.has_value()) return std::nullopt;

    rename::InstructionConfig config;
    config.geometry_input_port = *input;
    config.geometry_output_port = *output;

    std::map<std::string, std::string> adjacent_bindings;
    for (const auto& binding_data : json_object_array(node_data, "bindings")) {
        const auto type = json_string(binding_data, "typeId").value_or("");
        if (type != "edgeAdjacentBinding") return std::nullopt;
        const auto variable = json_string(binding_data, "variable").value_or("");
        if (variable.empty()) return std::nullopt;
        adjacent_bindings[variable] = binding_data;
    }

    if (const auto all_faces = json_string(node_data, "all_faces")) {
        config.all_faces_label = label_id_for(package, *all_faces);
        if (!all_faces->empty() && !config.all_faces_label.has_value()) return std::nullopt;
    }
    if (const auto all_edges = json_string(node_data, "all_edges")) {
        config.all_edges_label = label_id_for(package, *all_edges);
        if (!all_edges->empty() && !config.all_edges_label.has_value()) return std::nullopt;
    }

    const auto method = json_string(node_data, "method").value_or("manual");
    if (method == "condition" || method == "faces_condition") {
        for (const auto& condition_data : json_object_array(node_data, "faceConditions")) {
            const auto type = json_string(condition_data, "typeId").value_or("");
            if (type != "renameFaceByOpposite" && type != "renameByOpposite") {
                return std::nullopt;
            }
            const auto to = required_label(package, condition_data, "toLabel");
            if (!to.has_value()) return std::nullopt;
            rename::Condition condition;
            condition.target = rename::Target::faces;
            condition.to_label = *to;
            if (!set_optional_label(package, condition_data, "fromLabel", condition.from_label)
                || !set_optional_label(package, condition_data, "belongsTo", condition.owning_face_label)
                || !set_optional_label(package, condition_data, "belongsToEdge", condition.owning_edge_label)
                || !set_optional_label(package, condition_data, "oppFaceLabel", condition.opposite_face_label)
                || !set_optional_label(package, condition_data, "oppEdgeLabel", condition.opposite_edge_label)) {
                return std::nullopt;
            }
            config.conditions.push_back(condition);
        }
        return config.conditions.empty() ? std::nullopt : std::optional<rename::InstructionConfig>{config};
    }

    if (method == "edges_condition") {
        for (const auto& condition_data : json_object_array(node_data, "edgeConditions")) {
            const auto type = json_string(condition_data, "typeId").value_or("");
            const auto to = type == "renameEdgeToItsFace"
                ? std::optional<LabelId>{unassigned_label_id}
                : required_label(package, condition_data, "toLabel");
            if (!to.has_value()) return std::nullopt;
            rename::Condition condition;
            condition.target = rename::Target::directed_edges;
            condition.to_label = *to;
            condition.to_owning_face_label = type == "renameEdgeToItsFace";
            if (!set_optional_label(package, condition_data, "fromLabel", condition.from_label)
                || !set_optional_label(package, condition_data, "belongsTo", condition.owning_face_label)
                || !set_optional_label(package, condition_data, "belongsToEdge", condition.owning_edge_label)
                || !set_optional_label(package, condition_data, "oppFaceLabel", condition.opposite_face_label)
                || !set_optional_label(package, condition_data, "oppEdgeLabel", condition.opposite_edge_label)) {
                return std::nullopt;
            }
            if (type == "renameBorderEdge") {
                condition.border = true;
            } else if (type == "renameEdgeByOpposite") {
                // Opposite-label predicates were populated above.
            } else if (type == "renameEdgeByExpression" || type == "renameByExpression") {
                const auto expression = json_string(condition_data, "expression").value_or("");
                const auto binding = adjacent_bindings.find(expression);
                if (binding == adjacent_bindings.end()) return std::nullopt;
                if (!set_optional_label(package, binding->second, "label1", condition.adjacent_label1)
                    || !set_optional_label(package, binding->second, "label2", condition.adjacent_label2)) {
                    return std::nullopt;
                }
                condition.adjacent_relation =
                    rename_adjacent_relation(json_string(binding->second, "relation").value_or(""));
            } else if (type == "renameEdgeByLength"
                || type == "advancedRenameEdgeByLength"
                || type == "renameEdgeToItsFace") {
                condition.minimum_length = json_double(condition_data, "min").value_or(0.0);
                condition.maximum_length = json_double(
                    condition_data,
                    "max").value_or(std::numeric_limits<double>::max());
                condition.length_kind =
                    rename_length_kind(json_string(condition_data, "kind").value_or("any"));
            } else {
                return std::nullopt;
            }
            config.conditions.push_back(condition);
        }
        return config.conditions.empty() ? std::nullopt : std::optional<rename::InstructionConfig>{config};
    }

    const auto map_text = extract_array_text(node_data, "label_map");
    const std::regex item{
        R"regex(\{[^{}]*"from"\s*:\s*"([^"]*)"[^{}]*"to"\s*:\s*"([^"]*)"[^{}]*\})regex"};
    for (std::sregex_iterator it{map_text.begin(), map_text.end(), item};
         it != std::sregex_iterator{};
         ++it) {
        const auto from = label_id_for(package, (*it)[1].str());
        const auto to = label_id_for(package, (*it)[2].str());
        if (!from.has_value() || !to.has_value()) return std::nullopt;
        config.label_map[*from].push_back(*to);
    }

    return config;
}

std::optional<select::InstructionConfig> adapt_select(
    const LoadedMigratedPackage& package,
    const InstructionDescriptor& instruction,
    const std::string& node_data)
{
    auto input = first_input_port(instruction);
    if (!input.has_value()) return std::nullopt;

    const auto method = json_string(node_data, "method").value_or("bylabel");
    if (json_int(node_data, "primitive").value_or(0) != 0) {
        return std::nullopt;
    }

    select::InstructionConfig config;
    config.geometry_input_port = *input;
    if (auto output = find_output_port_named(instruction, "output")) {
        config.default_output_port = *output;
    } else if (auto first = first_output_port(instruction)) {
        config.default_output_port = *first;
    } else {
        return std::nullopt;
    }
    if (auto else_port = find_output_port_named(instruction, "else")) {
        config.else_output_port = *else_port;
    }

    config.limit.count = json_int(node_data, "ouputLimit").value_or(-1);
    config.limit.random_range = json_int(node_data, "ouputLimitRange").value_or(-1);
    config.limit.random_step = json_int(node_data, "ouputLimitStep").value_or(1);
    config.limit.percentage = json_bool(node_data, "outputLimitPct").value_or(false);

    if (method == "bylabel") {
        if (json_array_has_values(node_data, "conditions")) return std::nullopt;
        const auto outputs = named_select_output_ports(instruction);
        const auto label_outputs = json_string_array(node_data, "labelOutputs");
        for (std::size_t index = 0; index < label_outputs.size() && index < outputs.size(); ++index) {
            if (label_outputs[index].empty()) continue;
            const auto label = label_id_for(package, label_outputs[index]);
            if (!label.has_value()) return std::nullopt;
            config.label_routes.push_back(select::LabelRoute{*label, outputs[index]});
        }
        return config;
    }

    for (const auto& condition_data : json_object_array(node_data, "conditions")) {
        select::FaceCondition condition;
        const auto type = json_string(condition_data, "typeId").value_or("");
        if (type == "isLabeled") {
            if (!set_optional_label(package, condition_data, "label", condition.face_label)) {
                return std::nullopt;
            }
        } else if (type == "hasEdge" || type == "hasEdgeByLength" || type == "hasEdgeByOpposite") {
            if (!set_optional_label(package, condition_data, "label", condition.edge_label)
                || !set_optional_label(package, condition_data, "oppFace", condition.opposite_face_label)
                || !set_optional_label(package, condition_data, "oppEdge", condition.opposite_edge_label)) {
                return std::nullopt;
            }
            condition.minimum_edge_length = json_double(condition_data, "min").value_or(0.0);
            condition.maximum_edge_length = json_double(
                condition_data,
                "max").value_or(std::numeric_limits<double>::max());
        } else if (type == "hasBorderEdge") {
            if (!set_optional_label(package, condition_data, "label", condition.edge_label)) {
                return std::nullopt;
            }
            condition.require_border_edge = true;
        } else {
            return std::nullopt;
        }
        config.conditions.push_back(condition);
    }
    if (config.conditions.empty()) return std::nullopt;
    return config;
}

std::optional<loop::InstructionConfig> adapt_loop(
    const MigratedFunctionPackage& function,
    const InstructionDescriptor& instruction,
    const std::string& node_data)
{
    auto input = find_input_port_named(instruction, "input").value_or(
        first_input_port(instruction).value_or(PortId{}));
    auto output = find_output_port_named(instruction, "output").value_or(
        first_output_port(instruction).value_or(PortId{}));
    auto loop_output = find_output_port_named(instruction, "loop");
    if (input.empty() || output.empty() || !loop_output.has_value() || node_data.empty()) {
        return std::nullopt;
    }

    std::optional<PortId> body_input = input;
    for (const auto& edge : function.graph.edges) {
        if (edge.from_node == instruction.id && edge.from_port == *loop_output) {
            body_input = edge.to_port;
            break;
        }
    }

    const auto count = json_number_or_variable(function, node_data, "count");
    if (!count.has_value()) return std::nullopt;

    loop::InstructionConfig config;
    config.geometry_input_port = std::move(input);
    config.geometry_output_port = std::move(output);
    config.options.count = static_cast<std::int64_t>(*count);
    config.options.range = static_cast<std::int64_t>(
        json_number_or_variable(function, node_data, "range").value_or(-1.0));
    config.options.step = static_cast<std::int64_t>(
        json_number_or_variable(function, node_data, "step").value_or(-1.0));
    config.body.function = &function.graph;
    config.body.ports.input = *body_input;
    if (auto feedback = find_output_port_named(instruction, "loop")) {
        config.body.ports.feedback = *feedback;
    }
    if (auto all = find_output_port_named(instruction, "all")) {
        config.body.ports.all = *all;
    }
    if (auto body_output = find_output_port_named(instruction, "output")) {
        config.body.ports.output = *body_output;
    }
    config.variables = migrated_variable_plan(function, node_data);
    return config;
}

std::optional<inset::InstructionConfig> adapt_inset(
    const LoadedMigratedPackage& package,
    const MigratedFunctionPackage& function,
    const InstructionDescriptor& instruction,
    const std::string& node_data)
{
    auto input = first_input_port(instruction);
    auto output = first_output_port(instruction);
    if (!input.has_value() || !output.has_value() || node_data.empty()) {
        return std::nullopt;
    }
    const auto amount = json_number_or_variable(function, node_data, "amount");
    if (!amount.has_value() || *amount <= 0.0) return std::nullopt;

    inset::InstructionConfig config;
    config.geometry_input_port = *input;
    config.geometry_output_port = *output;
    config.amount = *amount;

    auto label = [&](const std::string& key) {
        return label_id_for(package, json_string(node_data, key).value_or(""))
            .value_or(unassigned_label_id);
    };
    config.labels.result_face = label("resultFace");
    config.labels.side_face = label("sideFace");
    config.labels.result_edge = label("resultEdge");
    const auto side_edge = label("sideEdge");
    config.labels.left_edge = label("left");
    config.labels.right_edge = label("right");
    config.labels.top_edge = label("top");
    config.labels.bottom_edge = label("bottom");
    if (config.labels.left_edge == unassigned_label_id) {
        config.labels.left_edge = side_edge != unassigned_label_id
            ? side_edge : label_id_for(package, "00000000-0000-0000-0000-000000000003")
                .value_or(unassigned_label_id);
    }
    if (config.labels.right_edge == unassigned_label_id) {
        config.labels.right_edge = side_edge != unassigned_label_id
            ? side_edge : label_id_for(package, "00000000-0000-0000-0000-000000000001")
                .value_or(unassigned_label_id);
    }
    if (config.labels.top_edge == unassigned_label_id) {
        config.labels.top_edge = side_edge != unassigned_label_id
            ? side_edge : label_id_for(package, "00000000-0000-0000-0000-000000000002")
                .value_or(unassigned_label_id);
    }
    if (config.labels.bottom_edge == unassigned_label_id) {
        config.labels.bottom_edge = side_edge != unassigned_label_id
            ? side_edge : label_id_for(package, "00000000-0000-0000-0000-000000000004")
                .value_or(unassigned_label_id);
    }
    return config;
}

void register_lod_handlers(
    MigratedInstructionRegistryResult& result,
    const std::map<InstructionKey, control::LodInstructionConfig>& configs)
{
    if (configs.empty()) return;

    result.registry.register_handler(
        "lod",
        [configs](const InstructionExecutionFrame& frame) {
            const auto it = configs.find(InstructionKey{
                frame.context.function_id,
                frame.inputs.node_id,
            });
            if (it == configs.end()) {
                InstructionResult missing;
                missing.node_id = frame.inputs.node_id;
                missing.failure_message = "Migrated LOD instruction has no decoded config.";
                return missing;
            }
            return control::make_lod_instruction_handler(it->second)(frame);
        });
    result.supported_kinds.insert("lod");
}

void register_if_handlers(
    MigratedInstructionRegistryResult& result,
    const std::map<InstructionKey, control::IfInstructionConfig>& configs)
{
    if (configs.empty()) return;

    result.registry.register_handler(
        "if",
        [configs](const InstructionExecutionFrame& frame) {
            const auto it = configs.find(InstructionKey{
                frame.context.function_id,
                frame.inputs.node_id,
            });
            if (it == configs.end()) {
                InstructionResult missing;
                missing.node_id = frame.inputs.node_id;
                missing.failure_message = "Migrated if instruction has no decoded config.";
                return missing;
            }
            return control::make_if_instruction_handler(it->second)(frame);
        });
    result.supported_kinds.insert("if");
}

void register_case_handlers(
    MigratedInstructionRegistryResult& result,
    const std::map<InstructionKey, control::CaseInstructionConfig>& configs)
{
    if (configs.empty()) return;

    result.registry.register_handler(
        "case",
        [configs](const InstructionExecutionFrame& frame) {
            const auto it = configs.find(InstructionKey{
                frame.context.function_id,
                frame.inputs.node_id,
            });
            if (it == configs.end()) {
                InstructionResult missing;
                missing.node_id = frame.inputs.node_id;
                missing.failure_message = "Migrated case instruction has no decoded config.";
                return missing;
            }
            return control::make_case_instruction_handler(it->second)(frame);
        });
    result.supported_kinds.insert("case");
}

void register_choice_handlers(
    MigratedInstructionRegistryResult& result,
    const std::map<InstructionKey, control::ChoiceInstructionConfig>& configs)
{
    if (configs.empty()) return;

    result.registry.register_handler(
        "choice",
        [configs](const InstructionExecutionFrame& frame) {
            const auto it = configs.find(InstructionKey{
                frame.context.function_id,
                frame.inputs.node_id,
            });
            if (it == configs.end()) {
                InstructionResult missing;
                missing.node_id = frame.inputs.node_id;
                missing.failure_message = "Migrated choice instruction has no decoded config.";
                return missing;
            }
            return control::make_choice_instruction_handler(it->second)(frame);
        });
    result.supported_kinds.insert("choice");
}

void register_loop_handlers(
    MigratedInstructionRegistryResult& result,
    const std::map<InstructionKey, loop::InstructionConfig>& configs)
{
    if (configs.empty()) return;

    result.registry.register_handler(
        "loop",
        [configs](const InstructionExecutionFrame& frame) {
            const auto it = configs.find(InstructionKey{
                frame.context.function_id,
                frame.inputs.node_id,
            });
            if (it == configs.end()) {
                InstructionResult missing;
                missing.node_id = frame.inputs.node_id;
                missing.failure_message = "Migrated loop instruction has no decoded config.";
                return missing;
            }
            return loop::make_instruction_handler(it->second)(frame);
        });
    result.supported_kinds.insert("loop");
}

void register_inset_handlers(
    MigratedInstructionRegistryResult& result,
    const std::map<InstructionKey, inset::InstructionConfig>& configs)
{
    if (configs.empty()) return;

    result.registry.register_handler(
        "inset",
        [configs](const InstructionExecutionFrame& frame) {
            const auto it = configs.find(InstructionKey{
                frame.context.function_id,
                frame.inputs.node_id,
            });
            if (it == configs.end()) {
                InstructionResult missing;
                missing.node_id = frame.inputs.node_id;
                missing.failure_message = "Migrated inset instruction has no decoded config.";
                return missing;
            }
            return inset::make_instruction_handler(it->second)(frame);
        });
    result.supported_kinds.insert("inset");
}

void register_merge_handlers(
    MigratedInstructionRegistryResult& result,
    const std::map<InstructionKey, merge::InstructionConfig>& configs)
{
    if (configs.empty()) return;

    result.registry.register_handler(
        "merge",
        [configs](const InstructionExecutionFrame& frame) {
            const auto it = configs.find(InstructionKey{
                frame.context.function_id,
                frame.inputs.node_id,
            });
            if (it == configs.end()) {
                InstructionResult missing;
                missing.node_id = frame.inputs.node_id;
                missing.failure_message = "Migrated merge instruction has no decoded config.";
                return missing;
            }
            return merge::make_instruction_handler(it->second)(frame);
        });
    result.supported_kinds.insert("merge");
}

void register_extrusion_handlers(
    MigratedInstructionRegistryResult& result,
    const std::map<InstructionKey, extrusion::InstructionConfig>& configs)
{
    if (configs.empty()) return;

    result.registry.register_handler(
        "extrusion",
        [configs](const InstructionExecutionFrame& frame) {
            const auto it = configs.find(InstructionKey{
                frame.context.function_id,
                frame.inputs.node_id,
            });
            if (it == configs.end()) {
                InstructionResult missing;
                missing.node_id = frame.inputs.node_id;
                missing.failure_message = "Migrated extrusion instruction has no decoded config.";
                return missing;
            }
            return extrusion::make_instruction_handler(it->second)(frame);
        });
    result.supported_kinds.insert("extrusion");
}

void register_rename_handlers(
    MigratedInstructionRegistryResult& result,
    const std::map<InstructionKey, rename::InstructionConfig>& configs)
{
    if (configs.empty()) return;

    result.registry.register_handler(
        "rename",
        [configs](const InstructionExecutionFrame& frame) {
            const auto it = configs.find(InstructionKey{
                frame.context.function_id,
                frame.inputs.node_id,
            });
            if (it == configs.end()) {
                InstructionResult missing;
                missing.node_id = frame.inputs.node_id;
                missing.failure_message = "Migrated rename instruction has no decoded config.";
                return missing;
            }
            return rename::make_instruction_handler(it->second)(frame);
        });
    result.supported_kinds.insert("rename");
}

void register_select_handlers(
    MigratedInstructionRegistryResult& result,
    const std::map<InstructionKey, select::InstructionConfig>& configs)
{
    if (configs.empty()) return;

    result.registry.register_handler(
        "select",
        [configs](const InstructionExecutionFrame& frame) {
            const auto it = configs.find(InstructionKey{
                frame.context.function_id,
                frame.inputs.node_id,
            });
            if (it == configs.end()) {
                InstructionResult missing;
                missing.node_id = frame.inputs.node_id;
                missing.failure_message = "Migrated select instruction has no decoded config.";
                return missing;
            }
            return select::make_instruction_handler(it->second)(frame);
        });
    result.supported_kinds.insert("select");
}

void register_partition_handlers(
    MigratedInstructionRegistryResult& result,
    const std::map<InstructionKey, partition::InstructionConfig>& configs)
{
    if (configs.empty()) return;

    result.registry.register_handler(
        "partition",
        [configs](const InstructionExecutionFrame& frame) {
            const auto it = configs.find(InstructionKey{
                frame.context.function_id,
                frame.inputs.node_id,
            });
            if (it == configs.end()) {
                InstructionResult missing;
                missing.node_id = frame.inputs.node_id;
                missing.failure_message = "Migrated partition instruction has no decoded config.";
                return missing;
            }
            return partition::make_instruction_handler(it->second)(frame);
        });
    result.supported_kinds.insert("partition");
}

} // namespace

MigratedInstructionRegistryResult make_migrated_instruction_registry(
    const LoadedMigratedPackage& package)
{
    MigratedInstructionRegistryResult result;
    std::map<InstructionKey, control::CaseInstructionConfig> case_configs;
    std::map<InstructionKey, control::ChoiceInstructionConfig> choice_configs;
    std::map<InstructionKey, control::IfInstructionConfig> if_configs;
    std::map<InstructionKey, inset::InstructionConfig> inset_configs;
    std::map<InstructionKey, control::LodInstructionConfig> lod_configs;
    std::map<InstructionKey, extrusion::InstructionConfig> extrusion_configs;
    std::map<InstructionKey, loop::InstructionConfig> loop_configs;
    std::map<InstructionKey, merge::InstructionConfig> merge_configs;
    std::map<InstructionKey, partition::InstructionConfig> partition_configs;
    std::map<InstructionKey, rename::InstructionConfig> rename_configs;
    std::map<InstructionKey, select::InstructionConfig> select_configs;

    for (const auto& entry : package.functions) {
        for (const auto& instruction : entry.second.graph.instructions) {
            if (instruction.kind == "output" || instruction.called_function_id.has_value()) {
                continue;
            }
            ++result.total_by_kind[instruction.kind];
            if (instruction.kind == "lod") {
                if (auto config = adapt_lod(instruction)) {
                    lod_configs.emplace(
                        InstructionKey{entry.first, instruction.id},
                        std::move(*config));
                    ++result.adapted_by_kind[instruction.kind];
                    continue;
                }
            }
            if (instruction.kind == "if") {
                if (auto config = adapt_if(
                    package,
                    entry.second,
                    instruction,
                    node_data_for(entry.second, instruction.id))) {
                    if_configs.emplace(
                        InstructionKey{entry.first, instruction.id},
                        std::move(*config));
                    ++result.adapted_by_kind[instruction.kind];
                    continue;
                }
            }
            if (instruction.kind == "case") {
                if (auto config = adapt_case(
                    package,
                    entry.second,
                    instruction,
                    node_data_for(entry.second, instruction.id))) {
                    case_configs.emplace(
                        InstructionKey{entry.first, instruction.id},
                        std::move(*config));
                    ++result.adapted_by_kind[instruction.kind];
                    continue;
                }
            }
            if (instruction.kind == "choice") {
                if (auto config = adapt_choice(
                    instruction,
                    node_data_for(entry.second, instruction.id))) {
                    choice_configs.emplace(
                        InstructionKey{entry.first, instruction.id},
                        std::move(*config));
                    ++result.adapted_by_kind[instruction.kind];
                    continue;
                }
            }
            if (instruction.kind == "loop") {
                if (auto config = adapt_loop(
                    entry.second,
                    instruction,
                    node_data_for(entry.second, instruction.id))) {
                    loop_configs.emplace(
                        InstructionKey{entry.first, instruction.id},
                        std::move(*config));
                    ++result.adapted_by_kind[instruction.kind];
                    continue;
                }
            }
            if (instruction.kind == "inset") {
                if (auto config = adapt_inset(
                    package,
                    entry.second,
                    instruction,
                    node_data_for(entry.second, instruction.id))) {
                    inset_configs.emplace(
                        InstructionKey{entry.first, instruction.id},
                        std::move(*config));
                    ++result.adapted_by_kind[instruction.kind];
                    continue;
                }
            }
            if (instruction.kind == "merge") {
                if (auto config = adapt_merge(instruction, node_data_for(entry.second, instruction.id))) {
                    merge_configs.emplace(
                        InstructionKey{entry.first, instruction.id},
                        std::move(*config));
                    ++result.adapted_by_kind[instruction.kind];
                    continue;
                }
            }
            if (instruction.kind == "extrusion") {
                auto adapted = adapt_extrusion(
                    package,
                    entry.second,
                    instruction,
                    node_data_for(entry.second, instruction.id));
                if (adapted.config.has_value()) {
                    extrusion_configs.emplace(
                        InstructionKey{entry.first, instruction.id},
                        std::move(*adapted.config));
                    ++result.adapted_by_kind[instruction.kind];
                    continue;
                }
                ++result.unsupported_reasons_by_kind[instruction.kind][adapted.reason];
                auto& examples = result.unsupported_examples_by_kind[instruction.kind];
                if (examples.size() < 8) {
                    std::ostringstream example;
                    example << "function=" << entry.first
                            << " node=" << instruction.id
                            << " reason=" << adapted.reason;
                    examples.push_back(example.str());
                }
            }
            if (instruction.kind == "rename") {
                if (auto config = adapt_rename(
                    package,
                    instruction,
                    node_data_for(entry.second, instruction.id))) {
                    rename_configs.emplace(
                        InstructionKey{entry.first, instruction.id},
                        std::move(*config));
                    ++result.adapted_by_kind[instruction.kind];
                    continue;
                }
            }
            if (instruction.kind == "partition") {
                auto adapted = adapt_partition(
                    package,
                    entry.second,
                    instruction,
                    node_data_for(entry.second, instruction.id));
                if (adapted.config.has_value()) {
                    partition_configs.emplace(
                        InstructionKey{entry.first, instruction.id},
                        std::move(*adapted.config));
                    ++result.adapted_by_kind[instruction.kind];
                    continue;
                }
                ++result.unsupported_reasons_by_kind[instruction.kind][adapted.reason];
                auto& examples = result.unsupported_examples_by_kind[instruction.kind];
                if (examples.size() < 8) {
                    std::ostringstream example;
                    example << "function=" << entry.first
                            << " node=" << instruction.id
                            << " reason=" << adapted.reason;
                    examples.push_back(example.str());
                }
            }
            if (instruction.kind == "select") {
                if (auto config = adapt_select(
                    package,
                    instruction,
                    node_data_for(entry.second, instruction.id))) {
                    select_configs.emplace(
                        InstructionKey{entry.first, instruction.id},
                        std::move(*config));
                    ++result.adapted_by_kind[instruction.kind];
                    continue;
                }
            }
            result.unsupported_kinds.insert(instruction.kind);
        }
    }

    register_case_handlers(result, case_configs);
    register_choice_handlers(result, choice_configs);
    register_if_handlers(result, if_configs);
    register_inset_handlers(result, inset_configs);
    register_lod_handlers(result, lod_configs);
    register_loop_handlers(result, loop_configs);
    register_extrusion_handlers(result, extrusion_configs);
    register_merge_handlers(result, merge_configs);
    register_partition_handlers(result, partition_configs);
    register_rename_handlers(result, rename_configs);
    register_select_handlers(result, select_configs);

    for (const auto& kind : result.unsupported_kinds) {
        if (result.supported_kinds.find(kind) == result.supported_kinds.end()) {
            result.registry.register_handler(kind, make_unsupported_handler(kind));
        }
    }

    return result;
}

} // namespace phoenix::migration
