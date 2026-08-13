#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/partition/production_adapter.hpp"

#include <functional>
#include <memory>

struct partition_model;

namespace phoenix::partition {

using ProductionModelRef = std::shared_ptr<partition_model>;
using ProductionModelFactory = std::function<ProductionModelRef()>;

struct InstructionConfig {
    PortId geometry_input_port = "geometry";
    PortId geometry_output_port = "result";
    ProductionModelFactory model_factory;
    std::map<std::string, double> values;
    std::map<int, LabelId> base_segment_labels;
};

// The factory must return a fresh production model. partition_model lazily
// caches its plan and is therefore not shared across faces or worker calls.
[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);

} // namespace phoenix::partition
