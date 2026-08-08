#pragma once

#include "phoenix/common.hpp"
#include "phoenix/graph.hpp"
#include "phoenix/values.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

namespace phoenix {

enum class InstructionState {
    idle,
    pending,
    ready,
    executing,
    completed,
};

struct ExecutionContext {
    FunctionId function_id;
    FunctionCallPath call_path;
    SeedValue global_seed = 0;
};

struct InstructionInputs {
    NodeId node_id = 0;
    std::vector<PortValue> promised_inputs;
};

struct InstructionResult {
    NodeId node_id = 0;
    std::vector<PortValue> produced_outputs;
    std::optional<std::string> failure_message;
};

struct InstructionExecutionFrame {
    ExecutionContext context;
    InstructionInputs inputs;
    std::optional<SeedValue> effective_seed;
};

struct NodeRuntimeState {
    NodeId node_id = 0;
    InstructionState state = InstructionState::idle;
    std::unordered_map<PortId, RuntimeValue> received_inputs;
};

} // namespace phoenix
