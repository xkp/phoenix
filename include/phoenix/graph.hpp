#pragma once

#include "phoenix/common.hpp"
#include "phoenix/randomness.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace phoenix {

enum class PortDirection {
    input,
    output,
};

struct PortDescriptor {
    PortId id;
    TypeId type;
    PortDirection direction = PortDirection::input;
};

struct EdgeDescriptor {
    NodeId from_node = 0;
    PortId from_port;
    NodeId to_node = 0;
    PortId to_port;
};

struct InstructionDescriptor {
    NodeId id = 0;
    std::string kind;
    std::vector<PortDescriptor> input_ports;
    std::vector<PortDescriptor> output_ports;
    bool generates_actor = false;
    bool multiplexes_input = false;
    bool has_else_port = true;
    bool failure_is_critical = false;
    bool enables_instancing = false;
    bool consumes_geometry = false;
    MultiplexSeedMode multiplex_seed_mode = MultiplexSeedMode::one_seed_for_all;
    std::optional<SeedValue> local_seed;
    std::optional<FunctionId> called_function_id;
    std::vector<LabelUid> referenced_label_uids;
};

struct FunctionDescriptor {
    FunctionId id;
    std::vector<PortDescriptor> input_ports;
    std::vector<PortDescriptor> output_ports;
    std::vector<InstructionDescriptor> instructions;
    std::vector<EdgeDescriptor> edges;
    bool generates_actor = false;
    std::optional<NodeId> output_node_id;
};

class GraphIndex {
public:
    explicit GraphIndex(const FunctionDescriptor& function);

    [[nodiscard]] const FunctionDescriptor& function() const noexcept;
    [[nodiscard]] const InstructionDescriptor* find_instruction(NodeId id) const noexcept;
    [[nodiscard]] const std::vector<const EdgeDescriptor*>& incoming_edges(NodeId id) const noexcept;
    [[nodiscard]] const std::vector<const EdgeDescriptor*>& outgoing_edges(NodeId id) const noexcept;

private:
    const FunctionDescriptor* function_ = nullptr;
    std::unordered_map<NodeId, const InstructionDescriptor*> by_id_;
    std::unordered_map<NodeId, std::vector<const EdgeDescriptor*>> incoming_edges_;
    std::unordered_map<NodeId, std::vector<const EdgeDescriptor*>> outgoing_edges_;
};

enum class GraphValidationCode {
    duplicate_function_input_port,
    duplicate_function_output_port,
    function_input_port_wrong_direction,
    function_output_port_wrong_direction,
    duplicate_instruction_id,
    duplicate_instruction_input_port,
    duplicate_instruction_output_port,
    instruction_input_port_wrong_direction,
    instruction_output_port_wrong_direction,
    edge_references_missing_source_node,
    edge_references_missing_target_node,
    edge_references_missing_source_port,
    edge_references_missing_target_port,
    edge_uses_non_output_source_port,
    edge_uses_non_input_target_port,
    edge_type_mismatch,
    instruction_missing_else_port,
    function_generates_actor_without_actor_instruction,
    cycle_detected,
};

struct GraphValidationIssue {
    GraphValidationCode code;
    std::string message;
    std::optional<NodeId> node_id;
    std::optional<std::size_t> edge_index;
};

struct GraphValidationResult {
    std::vector<GraphValidationIssue> issues;

    [[nodiscard]] bool ok() const noexcept
    {
        return issues.empty();
    }
};

class GraphValidator {
public:
    [[nodiscard]] GraphValidationResult validate(const FunctionDescriptor& function) const;
};

[[nodiscard]] std::string to_string(GraphValidationCode code);
[[nodiscard]] std::string format_issue(const GraphValidationIssue& issue);

} // namespace phoenix
