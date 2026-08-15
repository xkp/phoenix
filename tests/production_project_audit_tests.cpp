#include "phoenix/migration/production_project_audit.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path temp_root()
{
    auto root = std::filesystem::temp_directory_path() / "phoenix_p12_audit_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void write_text(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
}

bool has_diagnostic(
    const phoenix::migration::ProductionProjectAudit& audit,
    phoenix::migration::AuditDiagnosticCode code,
    const std::string& uid)
{
    for (const auto& diagnostic : audit.diagnostics) {
        if (diagnostic.code == code && diagnostic.label_uid == uid) return true;
    }
    return false;
}

bool test_reports_duplicate_and_conflicting_labels()
{
    const auto root = temp_root();
    const auto first_id = std::string{"FIRST@11111111-1111-1111-1111-111111111111"};
    const auto second_id = std::string{"SECOND@22222222-2222-2222-2222-222222222222"};
    const auto first_directory = root / "functions" / first_id;
    const auto second_directory = root / "functions" / second_id;

    write_text(first_directory / first_id,
        R"({"name":"FIRST","id":"FIRST@11111111-1111-1111-1111-111111111111","sceneId":"FIRST@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"},{"id":"BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB","name":"ROOF","visible":true,"color":"#222222"}]})");
    write_text(first_directory / (first_id + ".nodes"), R"({"nodes":[]})");
    write_text(second_directory / second_id,
        R"({"name":"SECOND","id":"SECOND@22222222-2222-2222-2222-222222222222","sceneId":"SECOND@22222222-2222-2222-2222-222222222222.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"},{"id":"BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB","name":"ROOF_CHANGED","visible":true,"color":"#222222"}]})");
    write_text(second_directory / (second_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionProjectAuditor auditor;
    const auto audit = auditor.audit({root});

    return audit.labels_by_uid.at("AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA").size() == 2
        && audit.labels_by_uid.at("BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB").size() == 2
        && audit.summary.function_manifest_count == 2
        && audit.summary.nodes_file_count == 2
        && audit.summary.duplicate_label_uid_count == 2
        && audit.summary.conflicting_label_uid_count == 1
        && !has_diagnostic(audit,
            phoenix::migration::AuditDiagnosticCode::conflicting_label_definition,
            "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA")
        && has_diagnostic(audit,
            phoenix::migration::AuditDiagnosticCode::conflicting_label_definition,
            "BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB");
}

bool test_reports_payload_blobs_repeated_function_calls_and_unresolved_labels()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto tool_id = std::string{"TOOL@22222222-2222-2222-2222-222222222222"};
    const auto project_directory = root / "projects" / project_id;
    const auto tool_directory = root / "functions" / tool_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes","labels":[{"id":"AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA","name":"WALL","visible":true,"color":"#111111"}]})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"TOOL@22222222-2222-2222-2222-222222222222"}},{"typeName":"function","data":{"file":"TOOL@22222222-2222-2222-2222-222222222222"}},{"typeName":"partition","data":{"file":"33333333-3333-3333-3333-333333333333"}},{"typeName":"select","data":{"labelOutputs":["CCCCCCCC-CCCC-CCCC-CCCC-CCCCCCCCCCCC"]}}]})");
    write_text(tool_directory / tool_id,
        R"({"name":"TOOL","id":"TOOL@22222222-2222-2222-2222-222222222222","sceneId":"TOOL@22222222-2222-2222-2222-222222222222.nodes"})");
    write_text(tool_directory / (tool_id + ".nodes"), R"({"nodes":[]})");

    const phoenix::migration::ProductionProjectAuditor auditor;
    const auto audit = auditor.audit({root / "projects"});

    return audit.function_call_counts.at(project_id).at(tool_id) == 2
        && audit.instruction_payload_blobs.at(project_id).count("33333333-3333-3333-3333-333333333333") == 1
        && audit.unresolved_label_references.at(project_id).count("CCCCCCCC-CCCC-CCCC-CCCC-CCCCCCCCCCCC") == 1
        && audit.summary.project_manifest_count == 1
        && audit.summary.function_manifest_count == 2
        && audit.summary.nodes_file_count == 2
        && audit.summary.local_function_reference_count == 2
        && audit.summary.imported_function_reference_count == 0
        && audit.summary.unresolved_imported_function_count == 0
        && audit.summary.instruction_payload_blob_reference_count == 1
        && audit.summary.unresolved_label_reference_count == 1
        && audit.summary.repeated_function_call_site_count == 2
        && has_diagnostic(audit,
            phoenix::migration::AuditDiagnosticCode::unresolved_label_reference,
            "CCCCCCCC-CCCC-CCCC-CCCC-CCCCCCCCCCCC");
}

bool test_summary_counts_unresolved_imported_function_references()
{
    const auto root = temp_root();
    const auto project_id = std::string{"HOUSE@11111111-1111-1111-1111-111111111111"};
    const auto missing_id = std::string{"MISSING@22222222-2222-2222-2222-222222222222"};
    const auto project_directory = root / "projects" / project_id;

    write_text(project_directory / project_id,
        R"({"name":"HOUSE","id":"HOUSE@11111111-1111-1111-1111-111111111111","sceneId":"HOUSE@11111111-1111-1111-1111-111111111111.nodes"})");
    write_text(project_directory / (project_id + ".nodes"),
        R"({"nodes":[{"typeName":"function","data":{"file":"MISSING@22222222-2222-2222-2222-222222222222"}}]})");

    const phoenix::migration::ProductionProjectAuditor auditor;
    const auto audit = auditor.audit({root / "projects"});

    return audit.summary.imported_function_reference_count == 1
        && audit.summary.unresolved_imported_function_count == 1
        && audit.discovery.unresolved_imported_function_ids.count(missing_id) == 1;
}

} // namespace

int main()
{
    const bool ok = test_reports_duplicate_and_conflicting_labels()
        && test_reports_payload_blobs_repeated_function_calls_and_unresolved_labels()
        && test_summary_counts_unresolved_imported_function_references();
    if (!ok) {
        std::cerr << "production project audit tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
