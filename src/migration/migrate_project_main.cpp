#include "phoenix/migration/production_migrated_package.hpp"
#include "phoenix/migration/production_migrated_package_io.hpp"

#include <filesystem>
#include <iostream>

namespace {

void print_usage()
{
    std::cerr << "usage: phoenix_migrate_project <production-root-or-projects-root> <output.phxmig>\n";
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

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        print_usage();
        return 2;
    }

    const std::filesystem::path input = argv[1];
    const std::filesystem::path output = argv[2];

    const phoenix::migration::ProductionMigrationReporter reporter;
    const auto report = reporter.build_report({input});

    std::cout << "projects: " << report.discovery.projects.size() << "\n";
    std::cout << "functions: " << report.linked_functions.functions.size() << "\n";
    std::cout << "label declarations: " << label_declaration_count(report) << "\n";
    std::cout << "labels finalized: " << report.labels.linked_labels.registry.size() << "\n";
    std::cout << "profile declarations: " << profile_declaration_count(report) << "\n";
    std::cout << "profiles finalized: " << report.profiles.profiles.size() << "\n";
    std::cout << "diagnostics: " << report.diagnostics.size()
              << " errors=" << report.error_count()
              << " warnings=" << report.warning_count() << "\n";

    if (!report.ok()) {
        for (const auto& diagnostic : report.diagnostics) {
            std::cerr << phoenix::migration::to_string(diagnostic.severity)
                      << " " << diagnostic.code;
            if (!diagnostic.function_id.empty()) std::cerr << " function=" << diagnostic.function_id;
            if (!diagnostic.label_uid.empty()) std::cerr << " label=" << diagnostic.label_uid;
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
    const auto write_diagnostics = writer.write(emitted.package, output);
    if (!write_diagnostics.empty()) {
        for (const auto& diagnostic : write_diagnostics) {
            std::cerr << "error package_io." << phoenix::migration::to_string(diagnostic.code)
                      << ": " << diagnostic.message << "\n";
        }
        return 1;
    }

    std::cout << "wrote: " << output.string() << "\n";
    return 0;
}
