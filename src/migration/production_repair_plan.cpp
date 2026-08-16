#include "phoenix/migration/production_repair_plan.hpp"

#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace phoenix::migration {
namespace {

std::string json_escape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const auto ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

std::string json_string(const std::string& value)
{
    return "\"" + json_escape(value) + "\"";
}

std::string stable_choice_id(std::size_t index)
{
    return "choice_" + std::to_string(index + 1);
}

std::string label_value_json(const LabelDefinition& definition)
{
    std::ostringstream output;
    output << "{"
           << "\"name\":" << json_string(definition.name) << ","
           << "\"color\":" << json_string(definition.color) << ","
           << "\"material\":" << json_string(definition.material) << ","
           << "\"visible\":" << (definition.hidden ? "false" : "true") << ","
           << "\"style\":" << json_string(definition.style)
           << "}";
    return output.str();
}

std::vector<ProductionRepairChoice> label_choices(
    const ProductionMigrationReport& report,
    const LabelUid& uid)
{
    struct ChoiceGroup {
        LabelDeclaration declaration;
        FunctionId function_id;
        std::size_t count = 0;
    };

    std::map<std::string, ChoiceGroup> by_value;
    for (const auto& function_entry : report.labels.declarations) {
        for (const auto& declaration : function_entry.second) {
            if (declaration.uid != uid) continue;
            const auto value = label_value_json(declaration.definition);
            auto& group = by_value[value];
            if (group.count == 0) {
                group.declaration = declaration;
                group.function_id = function_entry.first;
            }
            ++group.count;
        }
    }

    std::vector<ProductionRepairChoice> choices;
    for (const auto& entry : by_value) {
        const auto& group = entry.second;
        choices.push_back(ProductionRepairChoice{
            stable_choice_id(choices.size()),
            "Use label definition '" + group.declaration.definition.name + "'.",
            group.function_id,
            group.declaration.source,
            entry.first,
            group.count});
    }
    return choices;
}

std::vector<ProductionRepairChoice> profile_choices(
    const ProductionMigrationReport& report,
    const std::string& profile_id)
{
    struct ChoiceGroup {
        ProductionProfileDefinition definition;
        std::size_t count = 0;
    };

    std::map<std::string, ChoiceGroup> by_value;
    for (const auto& function_entry : report.profiles.declarations) {
        for (const auto& definition : function_entry.second) {
            if (definition.id != profile_id) continue;
            auto& group = by_value[definition.canonical_text];
            if (group.count == 0) group.definition = definition;
            ++group.count;
        }
    }

    std::vector<ProductionRepairChoice> choices;
    for (const auto& entry : by_value) {
        const auto& group = entry.second;
        choices.push_back(ProductionRepairChoice{
            stable_choice_id(choices.size()),
            "Use profile definition '" + group.definition.name + "'.",
            group.definition.function_id,
            group.definition.path,
            entry.first,
            group.count});
    }
    return choices;
}

void append_unresolved_function_repairs(
    ProductionRepairPlan& plan,
    const ProductionMigrationReport& report)
{
    for (const auto& function_id : report.linked_functions.unresolved_function_ids) {
        plan.items.push_back(ProductionRepairItem{
            "function:" + function_id,
            "function_link.unresolved_function_reference",
            function_id,
            {},
            "Missing production function reference. Either ignore the offending calls for this migration or install the function in production and rerun.",
            {
                ProductionRepairChoice{
                    "ignore_calls",
                    "Ignore unresolved calls, equivalent to disabling those production call nodes.",
                    {},
                    {},
                    R"({"action":"ignore_unresolved_function_calls"})",
                    0},
                ProductionRepairChoice{
                    "install_and_rerun",
                    "Install/publish the missing function in production, then rerun migration.",
                    {},
                    {},
                    R"({"action":"install_function_and_rerun"})",
                    0},
            }});
    }
}

void append_retired_instruction_repairs(
    ProductionRepairPlan& plan,
    const ProductionMigrationReport& report)
{
    for (const auto& diagnostic : report.diagnostics) {
        if (diagnostic.code != "instructions.retired_label_method"
            || diagnostic.function_id.empty()
            || diagnostic.node_id == 0) {
            continue;
        }
        plan.items.push_back(ProductionRepairItem{
            "instruction:" + diagnostic.function_id + ":" + std::to_string(diagnostic.node_id),
            diagnostic.code,
            diagnostic.function_id,
            std::to_string(diagnostic.node_id),
            "Retired production instruction. Ignore it for this migration, equivalent to disabling that production node and pruning disabled-only branches.",
            {
                ProductionRepairChoice{
                    "ignore_instruction",
                    "Ignore this instruction, equivalent to disabling the production node.",
                    diagnostic.function_id,
                    diagnostic.path,
                    R"({"action":"ignore_instruction"})",
                    1},
                ProductionRepairChoice{
                    "edit_source_and_rerun",
                    "Edit/replace the retired instruction in production, then rerun migration.",
                    diagnostic.function_id,
                    diagnostic.path,
                    R"({"action":"edit_source_and_rerun"})",
                    0},
            }});
    }
}

void append_label_repairs(
    ProductionRepairPlan& plan,
    const ProductionMigrationReport& report)
{
    std::set<LabelUid> seen;
    for (const auto& diagnostic : report.labels.linked_labels.diagnostics) {
        if (diagnostic.code != LabelDiagnosticCode::conflicting_definition || !diagnostic.uid) {
            continue;
        }
        if (!seen.insert(*diagnostic.uid).second) continue;
        plan.items.push_back(ProductionRepairItem{
            "label:" + *diagnostic.uid,
            "labels." + to_string(diagnostic.code),
            *diagnostic.uid,
            {},
            "Label UID has conflicting definitions. Choose the canonical definition for this migration.",
            label_choices(report, *diagnostic.uid)});
    }

    for (const auto& diagnostic : report.labels.linked_labels.diagnostics) {
        if (diagnostic.code != LabelDiagnosticCode::unresolved_reference
            || !diagnostic.uid
            || !diagnostic.function_id) {
            continue;
        }
        const auto choices = label_choices(report, *diagnostic.uid);
        if (choices.empty()) continue;
        plan.items.push_back(ProductionRepairItem{
            "label_ref:" + *diagnostic.function_id + ":" + *diagnostic.uid,
            "labels." + to_string(diagnostic.code),
            *diagnostic.uid,
            *diagnostic.function_id,
            "Function references a label UID that is declared elsewhere. Choose a definition to add to this function for this migration.",
            choices});
    }
}

void append_profile_repairs(
    ProductionRepairPlan& plan,
    const ProductionMigrationReport& report)
{
    std::set<std::string> seen;
    for (const auto& diagnostic : report.profiles.diagnostics) {
        if (diagnostic.code != ProductionProfileDiagnosticCode::conflicting_definition) continue;
        if (!seen.insert(diagnostic.profile_id).second) continue;
        plan.items.push_back(ProductionRepairItem{
            "profile:" + diagnostic.profile_id,
            "profiles." + to_string(diagnostic.code),
            diagnostic.profile_id,
            diagnostic.profile_name,
            "Profile ID has conflicting definitions. Choose the canonical definition for this migration.",
            profile_choices(report, diagnostic.profile_id)});
    }
}

} // namespace

ProductionRepairPlan ProductionRepairPlanBuilder::build(
    const ProductionMigrationReport& report) const
{
    ProductionRepairPlan plan;
    append_unresolved_function_repairs(plan, report);
    append_retired_instruction_repairs(plan, report);
    append_label_repairs(plan, report);
    append_profile_repairs(plan, report);
    return plan;
}

std::string ProductionRepairPlanJsonWriter::write(
    const ProductionRepairPlan& plan) const
{
    std::ostringstream output;
    output << "{\n"
           << "  \"schema\": \"PHOENIX_PRODUCTION_REPAIR_PLAN_V0\",\n"
           << "  \"items\": [\n";

    for (std::size_t item_index = 0; item_index < plan.items.size(); ++item_index) {
        const auto& item = plan.items[item_index];
        output << "    {\n"
               << "      \"repair_id\": " << json_string(item.repair_id) << ",\n"
               << "      \"diagnostic_code\": " << json_string(item.diagnostic_code) << ",\n"
               << "      \"subject_id\": " << json_string(item.subject_id) << ",\n"
               << "      \"subject_name\": " << json_string(item.subject_name) << ",\n"
               << "      \"description\": " << json_string(item.description) << ",\n"
               << "      \"choices\": [\n";

        for (std::size_t choice_index = 0; choice_index < item.choices.size(); ++choice_index) {
            const auto& choice = item.choices[choice_index];
            output << "        {\n"
                   << "          \"choice_id\": " << json_string(choice.choice_id) << ",\n"
                   << "          \"description\": " << json_string(choice.description) << ",\n"
                   << "          \"function_id\": " << json_string(choice.function_id) << ",\n"
                   << "          \"path\": " << json_string(choice.path.string()) << ",\n"
                   << "          \"occurrence_count\": " << choice.occurrence_count << ",\n"
                   << "          \"value\": " << choice.value_json << "\n"
                   << "        }";
            if (choice_index + 1 != item.choices.size()) output << ",";
            output << "\n";
        }

        output << "      ]\n"
               << "    }";
        if (item_index + 1 != plan.items.size()) output << ",";
        output << "\n";
    }

    output << "  ]\n"
           << "}\n";
    return output.str();
}

void ProductionRepairPlanJsonWriter::write_file(
    const ProductionRepairPlan& plan,
    const std::filesystem::path& path) const
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << write(plan);
}

} // namespace phoenix::migration
