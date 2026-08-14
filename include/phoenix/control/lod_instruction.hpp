#pragma once

#include "phoenix/execution.hpp"

#include <set>

namespace phoenix::control {

enum class LodLevel : std::int32_t { low=0, normal=1, high=2 };

struct LodInstructionConfig {
    PortId input_port = "input";
    PortId low_port = "low";
    PortId normal_port = "normal";
    PortId high_port = "high";
    std::set<LodLevel> connected_levels{LodLevel::low,LodLevel::normal,LodLevel::high};
};

[[nodiscard]] InstructionHandler make_lod_instruction_handler(LodInstructionConfig config = {});
[[nodiscard]] std::string configuration_revision(const LodInstructionConfig& config);

} // namespace phoenix::control
