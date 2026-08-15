#pragma once

#include "phoenix/migration/production_project_raw_load.hpp"

#include <map>
#include <string>
#include <vector>

namespace phoenix::migration {

struct LabelUidRemapOverride {
    LabelUid from;
    LabelUid to;
};

struct FunctionReferenceRewriteOverride {
    FunctionId from;
    FunctionId to;
};

struct IgnoredFunctionReferenceOverride {
    FunctionId function_id;
};

struct LabelDefinitionChoiceOverride {
    LabelUid uid;
    FunctionId target_function_id;
    std::string value_json;
};

struct ProfileDefinitionChoiceOverride {
    std::string profile_id;
    std::string value_json;
};

struct ProductionMigrationOverrides {
    std::vector<LabelUidRemapOverride> label_uid_remaps;
    std::vector<FunctionReferenceRewriteOverride> function_reference_rewrites;
    std::vector<IgnoredFunctionReferenceOverride> ignored_function_references;
    std::vector<LabelDefinitionChoiceOverride> label_definition_choices;
    std::vector<ProfileDefinitionChoiceOverride> profile_definition_choices;
};

struct AppliedMigrationOverride {
    std::string kind;
    std::string from;
    std::string to;
    FunctionId function_id;
    std::size_t replacement_count = 0;
};

struct OverrideApplicationResult {
    RawProductionProjectSet raw;
    std::vector<AppliedMigrationOverride> applied;
};

class ProductionMigrationOverrideApplier {
public:
    [[nodiscard]] OverrideApplicationResult apply(
        RawProductionProjectSet raw,
        const ProductionMigrationOverrides& overrides) const;
};

} // namespace phoenix::migration
