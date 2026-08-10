#pragma once

#include "phoenix/execution.hpp"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace phoenix {

class PartialRerunInstructionIndex final : public FunctionExecutionInstructionTraceSink {
public:
    void record_instruction(FunctionExecutionInstructionRecord instruction) override;

    [[nodiscard]] const std::vector<FunctionExecutionInstructionRecord>& instructions() const noexcept;
    [[nodiscard]] std::vector<std::size_t> find_by_function_and_node(
        const FunctionId& function_id,
        NodeId node_id) const;
    [[nodiscard]] std::vector<std::size_t> find_by_call_path_and_node(
        const FunctionCallPath& call_path,
        NodeId node_id) const;

private:
    std::vector<FunctionExecutionInstructionRecord> instructions_;
    std::unordered_map<std::string, std::vector<std::size_t>> by_function_and_node_;
    std::unordered_map<std::string, std::vector<std::size_t>> by_call_path_and_node_;
};

} // namespace phoenix
