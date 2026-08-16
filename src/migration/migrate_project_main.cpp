#include "phoenix/migration/production_migrated_package.hpp"
#include "phoenix/migration/production_migrated_package_io.hpp"
#include "phoenix/migration/production_repair_plan.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace {

void print_usage()
{
    std::cerr << "usage: phoenix_migrate_project [--interactive-repair] [--repair-selection <selection.json>] <production-root-or-projects-root> <output.phxmig>\n";
}

struct CliOptions {
    bool interactive_repair = false;
    std::filesystem::path repair_selection;
    std::filesystem::path input;
    std::filesystem::path output;
};

bool parse_options(int argc, char** argv, CliOptions& options)
{
    std::vector<std::string> positional;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--interactive-repair") {
            options.interactive_repair = true;
            continue;
        }
        if (arg == "--repair-selection") {
            if (index + 1 >= argc) return false;
            options.repair_selection = argv[++index];
            continue;
        }
        if (!arg.empty() && arg[0] == '-') return false;
        positional.push_back(arg);
    }
    if (positional.size() != 2) return false;
    options.input = positional[0];
    options.output = positional[1];
    return true;
}

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::size_t label_declaration_count(
    const phoenix::migration::ProductionMigrationReport& report)
{
    std::size_t count = 0;
    for (const auto& entry : report.labels.declarations) count += entry.second.size();
    return count;
}

std::size_t profile_declaration_count(
    const phoenix::migration::ProductionMigrationReport& report)
{
    std::size_t count = 0;
    for (const auto& entry : report.profiles.declarations) count += entry.second.size();
    return count;
}

std::string json_escape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const auto ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
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

void write_repair_selection_file(
    const std::filesystem::path& path,
    const std::vector<std::pair<std::string, std::string>>& selections)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << "{\n"
           << "  \"schema\": \"PHOENIX_PRODUCTION_REPAIR_SELECTION_V0\",\n"
           << "  \"selections\": [\n";
    for (std::size_t index = 0; index < selections.size(); ++index) {
        output << "    { \"repair_id\": " << json_string(selections[index].first)
               << ", \"choice_id\": " << json_string(selections[index].second)
               << " }";
        if (index + 1 != selections.size()) output << ",";
        output << "\n";
    }
    output << "  ]\n"
           << "}\n";
}

std::vector<std::pair<std::string, std::string>> read_repair_selection_file(
    const std::filesystem::path& path)
{
    std::vector<std::pair<std::string, std::string>> selections;
    const auto text = read_text_file(path);
    const std::regex pattern{
        R"regex("repair_id"\s*:\s*"([^"]+)"\s*,\s*"choice_id"\s*:\s*"([^"]+)")regex"};
    for (std::sregex_iterator it{text.begin(), text.end(), pattern}, end; it != end; ++it) {
        selections.emplace_back((*it)[1].str(), (*it)[2].str());
    }
    return selections;
}

const phoenix::migration::ProductionRepairChoice* find_choice(
    const phoenix::migration::ProductionRepairItem& item,
    const std::string& choice_id)
{
    for (const auto& choice : item.choices) {
        if (choice.choice_id == choice_id) return &choice;
    }
    return nullptr;
}

std::string prompt_repair_choice(
    const phoenix::migration::ProductionRepairItem& item)
{
    std::cout << "\nrepair: " << item.repair_id << "\n"
              << item.description << "\n";
    for (std::size_t index = 0; index < item.choices.size(); ++index) {
        const auto& choice = item.choices[index];
        std::cout << "  " << (index + 1) << ". " << choice.choice_id
                  << " - " << choice.description << "\n";
        if (!choice.function_id.empty()) std::cout << "     function: " << choice.function_id << "\n";
        if (!choice.path.empty()) std::cout << "     path: " << choice.path.string() << "\n";
        if (choice.occurrence_count != 0) std::cout << "     occurrences: " << choice.occurrence_count << "\n";
        std::cout << "     value: " << choice.value_json << "\n";
    }

    for (;;) {
        std::cout << "choose 1-" << item.choices.size() << ", choice_id, or blank to skip: ";
        std::string answer;
        if (!std::getline(std::cin, answer)) return {};
        if (answer.empty()) return {};

        try {
            const auto selected = static_cast<std::size_t>(std::stoul(answer));
            if (selected >= 1 && selected <= item.choices.size()) {
                return item.choices[selected - 1].choice_id;
            }
        } catch (...) {
        }

        if (find_choice(item, answer) != nullptr) return answer;
        std::cout << "invalid choice\n";
    }
}

std::vector<std::pair<std::string, std::string>> prompt_repair_selections(
    const phoenix::migration::ProductionRepairPlan& plan)
{
    std::vector<std::pair<std::string, std::string>> selections;
    for (const auto& item : plan.items) {
        const auto choice_id = prompt_repair_choice(item);
        if (!choice_id.empty()) selections.emplace_back(item.repair_id, choice_id);
    }
    return selections;
}

void write_repair_selections(
    const std::vector<std::pair<std::string, std::string>>& selections,
    const std::filesystem::path& output)
{
    const auto selection_path = output.string() + ".repair.selection.json";
    write_repair_selection_file(selection_path, selections);
    std::cerr << "repair_selection path=" << selection_path << "\n";
}

const phoenix::migration::ProductionRepairItem* find_item(
    const phoenix::migration::ProductionRepairPlan& plan,
    const std::string& repair_id)
{
    for (const auto& item : plan.items) {
        if (item.repair_id == repair_id) return &item;
    }
    return nullptr;
}

std::optional<phoenix::NodeId> parse_node_id(const std::string& text)
{
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(text, &consumed);
        if (consumed != text.size()) return std::nullopt;
        return static_cast<phoenix::NodeId>(value);
    } catch (...) {
        return std::nullopt;
    }
}

phoenix::migration::ProductionMigrationOverrides build_overrides_from_selections(
    const phoenix::migration::ProductionRepairPlan& plan,
    const std::vector<std::pair<std::string, std::string>>& selections)
{
    phoenix::migration::ProductionMigrationOverrides overrides;
    for (const auto& selection : selections) {
        const auto* item = find_item(plan, selection.first);
        if (item == nullptr) continue;
        const auto* choice = find_choice(*item, selection.second);
        if (choice == nullptr) continue;

        if (item->diagnostic_code == "function_link.unresolved_function_reference"
            && choice->choice_id == "ignore_calls") {
            overrides.ignored_function_references.push_back({item->subject_id});
            continue;
        }
        if (item->diagnostic_code == "instructions.retired_label_method"
            && choice->choice_id == "ignore_instruction") {
            const auto node_id = parse_node_id(item->subject_name);
            if (!node_id.has_value()) continue;
            overrides.ignored_instructions.push_back({item->subject_id, *node_id});
            continue;
        }
        if (item->diagnostic_code == "labels.conflicting_definition") {
            overrides.label_definition_choices.push_back({
                item->subject_id,
                {},
                choice->value_json});
            continue;
        }
        if (item->diagnostic_code == "labels.unresolved_reference") {
            overrides.label_definition_choices.push_back({
                item->subject_id,
                item->subject_name,
                choice->value_json});
            continue;
        }
        if (item->diagnostic_code == "profiles.conflicting_definition") {
            overrides.profile_definition_choices.push_back({
                item->subject_id,
                choice->value_json});
        }
    }
    return overrides;
}

void append_overrides(
    phoenix::migration::ProductionMigrationOverrides& target,
    phoenix::migration::ProductionMigrationOverrides source)
{
    target.label_uid_remaps.insert(
        target.label_uid_remaps.end(),
        source.label_uid_remaps.begin(),
        source.label_uid_remaps.end());
    target.function_reference_rewrites.insert(
        target.function_reference_rewrites.end(),
        source.function_reference_rewrites.begin(),
        source.function_reference_rewrites.end());
    target.ignored_function_references.insert(
        target.ignored_function_references.end(),
        source.ignored_function_references.begin(),
        source.ignored_function_references.end());
    target.ignored_instructions.insert(
        target.ignored_instructions.end(),
        source.ignored_instructions.begin(),
        source.ignored_instructions.end());
    target.label_definition_choices.insert(
        target.label_definition_choices.end(),
        source.label_definition_choices.begin(),
        source.label_definition_choices.end());
    target.profile_definition_choices.insert(
        target.profile_definition_choices.end(),
        source.profile_definition_choices.begin(),
        source.profile_definition_choices.end());
}

void print_summary(const phoenix::migration::ProductionMigrationReport& report)
{
    std::cout << "projects: " << report.discovery.projects.size() << "\n";
    std::cout << "functions: " << report.linked_functions.functions.size() << "\n";
    std::cout << "label declarations: " << label_declaration_count(report) << "\n";
    std::cout << "labels finalized: " << report.labels.linked_labels.registry.size() << "\n";
    std::cout << "profile declarations: " << profile_declaration_count(report) << "\n";
    std::cout << "profiles finalized: " << report.profiles.profiles.size() << "\n";
    std::cout << "diagnostics: " << report.diagnostics.size()
              << " errors=" << report.error_count()
              << " warnings=" << report.warning_count() << "\n";
}

std::vector<std::pair<std::string, std::string>> selections_for_plan(
    const phoenix::migration::ProductionRepairPlan& plan,
    const std::vector<std::pair<std::string, std::string>>& saved_selections)
{
    std::vector<std::pair<std::string, std::string>> selections;
    for (const auto& saved : saved_selections) {
        const auto* item = find_item(plan, saved.first);
        if (item == nullptr || find_choice(*item, saved.second) == nullptr) continue;
        selections.push_back(saved);
    }
    return selections;
}

phoenix::migration::ProductionMigrationReport apply_saved_repair_selections(
    const phoenix::migration::ProductionMigrationReporter& reporter,
    const std::filesystem::path& input,
    phoenix::migration::ProductionMigrationReport report,
    const std::vector<std::pair<std::string, std::string>>& saved_selections)
{
    phoenix::migration::ProductionMigrationOverrides cumulative_overrides;

    for (;;) {
        if (report.ok()) return report;
        const auto repair_plan = phoenix::migration::ProductionRepairPlanBuilder{}.build(report);
        if (repair_plan.empty()) return report;

        const auto selections = selections_for_plan(repair_plan, saved_selections);
        if (selections.empty()) return report;

        const auto before_errors = report.error_count();
        append_overrides(
            cumulative_overrides,
            build_overrides_from_selections(repair_plan, selections));
        report = reporter.build_report({input}, cumulative_overrides);
        std::cout << "\nrepaired report:\n";
        print_summary(report);
        if (report.error_count() >= before_errors && !report.ok()) return report;
    }
}

} // namespace

int main(int argc, char** argv)
{
    CliOptions options;
    if (!parse_options(argc, argv, options)) {
        print_usage();
        return 2;
    }

    const phoenix::migration::ProductionMigrationReporter reporter;
    auto report = reporter.build_report({options.input});
    print_summary(report);

    if (!report.ok() && !options.repair_selection.empty()) {
        const auto saved_selections = read_repair_selection_file(options.repair_selection);
        if (saved_selections.empty()) {
            std::cerr << "error repair_selection: no selections found in "
                      << options.repair_selection.string() << "\n";
            return 2;
        }
        report = apply_saved_repair_selections(
            reporter,
            options.input,
            std::move(report),
            saved_selections);
    }

    if (!report.ok() && !options.interactive_repair) {
        const auto repair_path = options.output.string() + ".repair.json";
        const auto repair_plan = phoenix::migration::ProductionRepairPlanBuilder{}.build(report);
        if (!repair_plan.empty()) {
            phoenix::migration::ProductionRepairPlanJsonWriter{}.write_file(
                repair_plan,
                repair_path);
            std::cerr << "repair_choices path=" << repair_path << "\n";
        }
    }

    if (!report.ok() && options.interactive_repair) {
        phoenix::migration::ProductionMigrationOverrides cumulative_overrides;
        std::vector<std::pair<std::string, std::string>> cumulative_selections;

        for (;;) {
            const auto repair_path = options.output.string() + ".repair.json";
            const auto repair_plan = phoenix::migration::ProductionRepairPlanBuilder{}.build(report);
            if (repair_plan.empty()) break;
            phoenix::migration::ProductionRepairPlanJsonWriter{}.write_file(
                repair_plan,
                repair_path);
            std::cerr << "repair_choices path=" << repair_path << "\n";

            const auto selections = prompt_repair_selections(repair_plan);
            if (selections.empty()) break;
            cumulative_selections.insert(
                cumulative_selections.end(),
                selections.begin(),
                selections.end());
            write_repair_selections(cumulative_selections, options.output);

            append_overrides(
                cumulative_overrides,
                build_overrides_from_selections(repair_plan, selections));
            report = reporter.build_report({options.input}, cumulative_overrides);
            std::cout << "\nrepaired report:\n";
            print_summary(report);
            if (report.ok()) break;
        }
        if (cumulative_selections.empty()) {
            write_repair_selections(cumulative_selections, options.output);
        }
    }

    if (!report.ok()) {
        for (const auto& diagnostic : report.diagnostics) {
            std::cerr << phoenix::migration::to_string(diagnostic.severity)
                      << " " << diagnostic.code;
            if (!diagnostic.function_id.empty()) std::cerr << " function=" << diagnostic.function_id;
            if (!diagnostic.label_uid.empty()) std::cerr << " label=" << diagnostic.label_uid;
            if (diagnostic.node_id != 0) std::cerr << " node=" << diagnostic.node_id;
            if (!diagnostic.path.empty()) std::cerr << " path=" << diagnostic.path.string();
            std::cerr << ": " << diagnostic.message << "\n";
        }
        return 1;
    }

    const phoenix::migration::MigratedProjectPackageBuilder package_builder;
    const auto emitted = package_builder.build(report);
    if (!emitted.ok()) {
        for (const auto& diagnostic : emitted.diagnostics) {
            std::cerr << "error package." << phoenix::migration::to_string(diagnostic.code)
                      << ": " << diagnostic.message << "\n";
        }
        return 1;
    }

    const phoenix::migration::MigratedProjectPackageWriter writer;
    const auto write_diagnostics = writer.write(emitted.package, options.output);
    if (!write_diagnostics.empty()) {
        for (const auto& diagnostic : write_diagnostics) {
            std::cerr << "error package_io." << phoenix::migration::to_string(diagnostic.code)
                      << ": " << diagnostic.message << "\n";
        }
        return 1;
    }

    std::cout << "wrote: " << options.output.string() << "\n";
    return 0;
}
