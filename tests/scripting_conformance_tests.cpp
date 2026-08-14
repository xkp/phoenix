#include "phoenix/scripting/conformance.hpp"

#include <iostream>
#include <set>

namespace {

bool corpus_is_complete_and_unique()
{
    const auto& corpus = phoenix::scripting::version_one_conformance_corpus();
    std::set<std::string> ids;
    std::set<phoenix::scripting::ConformanceFeature> features;
    for (const auto& test : corpus) {
        ids.insert(test.id);
        features.insert(test.feature);
    }
    return ids.size() == corpus.size() && features.size() == 12;
}

bool migrates_production_bracket_names()
{
    using namespace phoenix::scripting;
    const auto migrated = migrate_legacy_bracket_bindings(
        "[wall height] * [bay-width] + [wall height]",
        {{"wall height", 3.0}, {"bay-width", 4.0}});
    return migrated.success && migrated.bindings.size() == 2
        && migrated.aliases.size() == 2
        && migrated.source.find('[') == std::string::npos
        && migrated.source.find(migrated.aliases.at("wall height")) != std::string::npos;
}

bool rejects_unknown_legacy_bindings()
{
    const auto result = phoenix::scripting::migrate_legacy_bracket_bindings("[missing] + 1", {});
    return !result.success && result.diagnostics.size() == 1;
}

} // namespace

int main()
{
    const bool corpus = corpus_is_complete_and_unique();
    const bool migration = migrates_production_bracket_names();
    const bool errors = rejects_unknown_legacy_bindings();
    std::cout << "script conformance coverage: " << corpus << '\n'
              << "script legacy bracket migration: " << migration << '\n'
              << "script legacy migration errors: " << errors << '\n';
    return corpus && migration && errors ? 0 : 1;
}
