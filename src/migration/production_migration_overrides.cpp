#include "phoenix/migration/production_migration_overrides.hpp"

namespace phoenix::migration {
namespace {

std::size_t replace_all(std::string& text, const std::string& from, const std::string& to)
{
    if (from.empty()) return 0;
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
        ++count;
    }
    return count;
}

void rewrite_references(
    std::set<FunctionId>& references,
    const FunctionId& from,
    const FunctionId& to)
{
    const auto erased = references.erase(from);
    if (erased != 0) references.insert(to);
}

} // namespace

OverrideApplicationResult ProductionMigrationOverrideApplier::apply(
    RawProductionProjectSet raw,
    const ProductionMigrationOverrides& overrides) const
{
    OverrideApplicationResult result;
    result.raw = std::move(raw);

    for (auto& entry : result.raw.functions) {
        for (auto& function : entry.second) {
            for (const auto& override : overrides.label_uid_remaps) {
                std::size_t count = 0;
                count += replace_all(function.manifest_text, override.from, override.to);
                count += replace_all(function.nodes_text, override.from, override.to);
                for (auto& payload : function.payload_blobs) {
                    count += replace_all(payload.second.text, override.from, override.to);
                }
                if (count != 0) {
                    result.applied.push_back(AppliedMigrationOverride{
                        "label_uid_remap",
                        override.from,
                        override.to,
                        function.candidate.id,
                        count});
                }
            }

            for (const auto& override : overrides.function_reference_rewrites) {
                std::size_t count = 0;
                count += replace_all(function.manifest_text, override.from, override.to);
                count += replace_all(function.nodes_text, override.from, override.to);
                rewrite_references(
                    function.candidate.referenced_function_ids,
                    override.from,
                    override.to);
                if (count != 0) {
                    result.applied.push_back(AppliedMigrationOverride{
                        "function_reference_rewrite",
                        override.from,
                        override.to,
                        function.candidate.id,
                        count});
                }
            }
        }
    }

    for (auto& project : result.raw.projects) {
        for (const auto& override : overrides.function_reference_rewrites) {
            rewrite_references(project.candidate.declared_function_ids, override.from, override.to);
        }
    }

    for (const auto& override : overrides.function_reference_rewrites) {
        rewrite_references(
            result.raw.discovery.imported_function_references,
            override.from,
            override.to);
        rewrite_references(
            result.raw.discovery.unresolved_imported_function_ids,
            override.from,
            override.to);
    }

    return result;
}

} // namespace phoenix::migration
