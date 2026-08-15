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

struct ProductionMigrationOverrides {
    std::vector<LabelUidRemapOverride> label_uid_remaps;
    std::vector<FunctionReferenceRewriteOverride> function_reference_rewrites;
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
