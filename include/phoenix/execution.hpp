#pragma once

#include "phoenix/actors.hpp"
#include "phoenix/common.hpp"
#include "phoenix/graph.hpp"
#include "phoenix/randomness.hpp"
#include "phoenix/values.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
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

enum class FunctionExecutionStatus {
    completed,
    invalid_request,
    invalid_graph,
    missing_handler,
    deadlocked,
    failed,
};

enum class ExecutionTraceLevel {
    none,
    scope,
    instruction,
    item,
    value,
};

struct ExecutionContext {
    FunctionId function_id;
    FunctionCallPath call_path;
    SeedValue global_seed = 0;
};

struct CallFrame {
    FunctionId function_id;
    FunctionCallPath call_path;
    std::optional<NodeId> caller_node_id;
    std::optional<ActorId> actor_id;
};

class CallStack {
public:
    void push(CallFrame frame);
    [[nodiscard]] std::optional<CallFrame> pop();

    [[nodiscard]] const CallFrame* current() const noexcept;
    [[nodiscard]] const std::vector<CallFrame>& frames() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<CallFrame> frames_;
};

struct InstructionInputs {
    NodeId node_id = 0;
    std::vector<PortValue> promised_inputs;
};

struct InstructionFailure {
    NodeId node_id = 0;
    std::optional<std::uint64_t> item_key;
    std::string message;
    std::vector<PortValue> input_context;
    CallStack call_stack;
};

struct InstructionResult {
    NodeId node_id = 0;
    std::vector<PortValue> produced_outputs;
    std::optional<std::string> failure_message;
    std::vector<InstructionFailure> failures;
};

struct InstructionExecutionFrame {
    ExecutionContext context;
    CallStack call_stack;
    InstructionInputs inputs;
    SeedDerivationInput seed_derivation;
    std::optional<SeedValue> effective_seed;
    MultiplexSeedMode multiplex_seed_mode = MultiplexSeedMode::one_seed_for_all;

    [[nodiscard]] SeedValue derive_item_seed(std::uint64_t item_key) const noexcept;
};

struct NodeRuntimeState {
    NodeId node_id = 0;
    InstructionState state = InstructionState::idle;
    std::vector<PortState> input_ports;
    std::unordered_map<PortId, RuntimeValue> received_inputs;
};

using InstructionHandler = std::function<InstructionResult(const InstructionExecutionFrame&)>;

class InstructionRegistry {
public:
    void register_handler(std::string kind, InstructionHandler handler);

    [[nodiscard]] const InstructionHandler* find_handler(const std::string& kind) const noexcept;

private:
    std::unordered_map<std::string, InstructionHandler> handlers_;
};

class FunctionLibrary {
public:
    void register_function(const FunctionDescriptor& function);

    [[nodiscard]] const FunctionDescriptor* find_function(const FunctionId& id) const noexcept;

private:
    std::unordered_map<FunctionId, const FunctionDescriptor*> functions_;
};

struct FunctionExecutionScopeRecord {
    FunctionId function_id;
    const FunctionDescriptor* function = nullptr;
    FunctionCallPath call_path;
    ActorId actor_id;
    bool generates_actor = false;
    std::optional<NodeId> caller_node_id;
    std::optional<std::size_t> parent_scope_index;
    std::vector<PortValue> inputs;
    std::unordered_map<NodeId, std::vector<PortValue>> input_defaults;
    SeedValue global_seed = 0;
};

class FunctionExecutionScopeTraceSink {
public:
    virtual ~FunctionExecutionScopeTraceSink() = default;

    [[nodiscard]] virtual std::size_t record_scope(FunctionExecutionScopeRecord scope) = 0;
};

struct FunctionExecutionInstructionRecord {
    FunctionId function_id;
    FunctionCallPath call_path;
    NodeId node_id = 0;
    std::string instruction_kind;
    std::optional<ActorId> actor_id;
};

class FunctionExecutionInstructionTraceSink {
public:
    virtual ~FunctionExecutionInstructionTraceSink() = default;

    virtual void record_instruction(FunctionExecutionInstructionRecord instruction) = 0;
};

struct FunctionExecutionRequest {
    const FunctionDescriptor* function = nullptr;
    std::vector<PortValue> inputs;
    std::unordered_map<NodeId, std::vector<PortValue>> input_defaults;
    ExecutionContext context;
    CallStack call_stack;
    ExecutionTraceLevel trace_level = ExecutionTraceLevel::none;
    FunctionExecutionScopeTraceSink* scope_trace_sink = nullptr;
    FunctionExecutionInstructionTraceSink* instruction_trace_sink = nullptr;
    std::optional<std::size_t> parent_scope_index;
};

struct FunctionExecutionResult {
    FunctionExecutionStatus status = FunctionExecutionStatus::completed;
    std::vector<PortValue> outputs;
    std::vector<NodeRuntimeState> node_states;
    std::optional<ActorNode> actor;
    std::vector<InstructionFailure> failures;
    std::optional<std::string> failure_message;
};

class FunctionExecutor {
public:
    explicit FunctionExecutor(const InstructionRegistry& registry);
    FunctionExecutor(const InstructionRegistry& registry, const FunctionLibrary& function_library);

    [[nodiscard]] FunctionExecutionResult run(const FunctionExecutionRequest& request) const;

private:
    const InstructionRegistry* registry_ = nullptr;
    const FunctionLibrary* function_library_ = nullptr;
    SeedDeriver seed_deriver_;
};

[[nodiscard]] std::string to_string(InstructionState state);
[[nodiscard]] std::string to_string(FunctionExecutionStatus status);
[[nodiscard]] std::string to_string(ExecutionTraceLevel level);

} // namespace phoenix
