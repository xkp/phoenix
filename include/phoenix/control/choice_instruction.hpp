#pragma once

#include "phoenix/execution.hpp"

#include <optional>
#include <string>
#include <vector>

namespace phoenix::control {

struct ChoiceItem {
    PortId output_port;
    std::string description;
    std::string image;
};

struct ChoiceInstructionConfig {
    PortId input_port = "input";
    std::vector<ChoiceItem> items;
    std::optional<PortId> selected_output;
    std::string published_name;
    std::string published_group;
    bool always_visible = false;
    bool important = false;
};

[[nodiscard]] InstructionHandler make_choice_instruction_handler(
    ChoiceInstructionConfig config);
[[nodiscard]] std::string configuration_revision(const ChoiceInstructionConfig& config);

} // namespace phoenix::control
