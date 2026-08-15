#pragma once

#include "phoenix/labels.hpp"
#include "phoenix/migration/production_project_discovery.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace phoenix::migration {

enum class AuditDiagnosticCode {
    conflicting_label_definition,
    unresolved_label_reference,
    unreadable_file,
};

struct AuditDiagnostic {
    AuditDiagnosticCode code;
    std::string message;
    std::filesystem::path path;
    FunctionId function_id;
    LabelUid label_uid;
};

struct ProductionLabelOccurrence {
    LabelUid uid;
    LabelDefinition definition;
    FunctionId function_id;
    std::filesystem::path source_path;
};

struct ProductionProjectAuditSummary {
    std::size_t project_manifest_count = 0;
    std::size_t function_manifest_count = 0;
    std::size_t nodes_file_count = 0;
    std::size_t local_function_reference_count = 0;
    std::size_t imported_function_reference_count = 0;
    std::size_t unresolved_imported_function_count = 0;
    std::size_t duplicate_label_uid_count = 0;
    std::size_t conflicting_label_uid_count = 0;
    std::size_t unresolved_label_reference_count = 0;
    std::size_t instruction_payload_blob_reference_count = 0;
    std::size_t repeated_function_call_site_count = 0;
};

struct ProductionProjectAudit {
    ProductionProjectDiscovery discovery;
    ProductionProjectAuditSummary summary;
    std::map<LabelUid, std::vector<ProductionLabelOccurrence>> labels_by_uid;
    std::map<FunctionId, std::map<FunctionId, std::size_t>> function_call_counts;
    std::map<FunctionId, std::set<std::string>> instruction_payload_blobs;
    std::map<FunctionId, std::set<LabelUid>> unresolved_label_references;
    std::vector<AuditDiagnostic> diagnostics;
};

class ProductionProjectAuditor {
public:
    [[nodiscard]] ProductionProjectAudit audit(
        const std::vector<std::filesystem::path>& roots) const;
};

[[nodiscard]] std::string to_string(AuditDiagnosticCode code);

} // namespace phoenix::migration
