#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/extrusion/kernel.hpp"

namespace phoenix::extrusion {

struct StageMetrics {
    std::uint64_t preparation_microseconds = 0;
    std::uint64_t kernel_microseconds = 0;
    std::uint64_t demotion_microseconds = 0;
    std::uint64_t repair_microseconds = 0;
    std::size_t item_count = 0;
    std::size_t succeeded_item_count = 0;
};

class StageMetricsSink {
public:
    virtual ~StageMetricsSink() = default;
    virtual void record_extrusion_stages(StageMetrics metrics) = 0;
};

struct InstructionConfig {
    PortId geometry_input_port = "geometry";
    PortId geometry_output_port = "result";
    ProfileRef profile;
    LabelId bottom_label = unassigned_label_id;
    LabelId right_label = unassigned_label_id;
    LabelId top_label = unassigned_label_id;
    LabelId left_label = unassigned_label_id;
    LabelId skirt_label = unassigned_label_id;
    LabelId cap_label = unassigned_label_id;
    RepairPolicy repair_policy;
    StageMetricsSink* metrics_sink = nullptr;
};

[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);

} // namespace phoenix::extrusion
