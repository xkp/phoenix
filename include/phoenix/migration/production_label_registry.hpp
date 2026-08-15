#pragma once

#include "phoenix/labels.hpp"
#include "phoenix/migration/production_project_raw_load.hpp"

#include <map>
#include <vector>

namespace phoenix::migration {

struct ProductionLabelRegistryBuild {
    LinkedLabels linked_labels;
    FunctionLabelDeclarations declarations;

    [[nodiscard]] bool ok() const noexcept { return linked_labels.ok(); }
};

class ProductionLabelRegistryBuilder {
public:
    [[nodiscard]] ProductionLabelRegistryBuild build(
        const RawProductionProjectSet& raw) const;
};

} // namespace phoenix::migration
