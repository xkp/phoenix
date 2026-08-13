#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/instance/placement.hpp"

namespace phoenix::instance {

struct ExternalPrototypeIdentity {
    std::string asset_uid;
    std::uint64_t asset_version = 0;
    std::uint64_t content_fingerprint = 0;
    std::optional<std::string> display_name;
    [[nodiscard]] bool valid() const noexcept
    { return !asset_uid.empty() && content_fingerprint != 0; }
    [[nodiscard]] ActorId stable_id() const;
};

struct RangeStep {
    double value = 0.0;
    std::optional<double> range;
    double step = -1.0;
};

struct TransformRanges {
    RangeStep rotation_x, rotation_y, rotation_z;
    RangeStep scale_x{1.0}, scale_y{1.0}, scale_z{1.0};
    RangeStep translation_x, translation_y, translation_z;
};

struct InstructionConfig {
    PortId geometry_input_port = "input";
    PortId output_port = "output";
    ExternalPrototypeIdentity prototype;
    PlacementOptions placement;
    TransformRanges ranges;
    bool one_seed_each = false;
    bool remove_input = false;
};

[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);
[[nodiscard]] std::string configuration_revision(const InstructionConfig& config);

} // namespace phoenix::instance
