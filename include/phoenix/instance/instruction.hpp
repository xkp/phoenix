#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/instance/placement.hpp"
#include "phoenix/scripting/numeric_value.hpp"

#include <utility>

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
    scripting::NumericRange value;
    RangeStep() = default;
    explicit RangeStep(scripting::NumericRange range) : value(std::move(range)) {}
    RangeStep(double minimum, std::optional<double> maximum = std::nullopt, double step = -1.0)
        : value{
            scripting::numeric_literal(minimum),
            maximum ? std::optional<scripting::NumericValue>{
                scripting::numeric_literal(*maximum)}
                    : std::nullopt,
            step > 0.0 ? std::optional<scripting::NumericValue>{
                scripting::numeric_literal(step)}
                    : std::nullopt}
    {
    }
};

struct TransformRanges {
    RangeStep rotation_x{0.0};
    RangeStep rotation_y{0.0};
    RangeStep rotation_z{0.0};
    RangeStep scale_x{1.0};
    RangeStep scale_y{1.0};
    RangeStep scale_z{1.0};
    RangeStep translation_x{0.0};
    RangeStep translation_y{0.0};
    RangeStep translation_z{0.0};
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
