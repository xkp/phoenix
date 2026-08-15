#pragma once

#include "phoenix/migration/production_migration_report.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace phoenix::migration {

struct ProductionRepairChoice {
    std::string choice_id;
    std::string description;
    FunctionId function_id;
    std::filesystem::path path;
    std::string value_json;
    std::size_t occurrence_count = 0;
};

struct ProductionRepairItem {
    std::string repair_id;
    std::string diagnostic_code;
    std::string subject_id;
    std::string subject_name;
    std::string description;
    std::vector<ProductionRepairChoice> choices;
};

struct ProductionRepairPlan {
    std::vector<ProductionRepairItem> items;

    [[nodiscard]] bool empty() const noexcept { return items.empty(); }
};

class ProductionRepairPlanBuilder {
public:
    [[nodiscard]] ProductionRepairPlan build(
        const ProductionMigrationReport& report) const;
};

class ProductionRepairPlanJsonWriter {
public:
    [[nodiscard]] std::string write(const ProductionRepairPlan& plan) const;
    void write_file(
        const ProductionRepairPlan& plan,
        const std::filesystem::path& path) const;
};

} // namespace phoenix::migration
