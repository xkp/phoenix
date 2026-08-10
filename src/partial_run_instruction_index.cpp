#include "phoenix/partial_run_instruction_index.hpp"

#include <sstream>
#include <utility>

namespace phoenix {
namespace {

void append_field(std::ostringstream& stream, const std::string& value)
{
    stream << value.size() << ':' << value;
}

std::string function_node_key(const FunctionId& function_id, NodeId node_id)
{
    std::ostringstream stream;
    append_field(stream, function_id);
    stream << '|' << node_id;
    return stream.str();
}

std::string call_path_node_key(const FunctionCallPath& call_path, NodeId node_id)
{
    std::ostringstream stream;
    stream << call_path.size();
    for (const auto& segment : call_path) {
        stream << '|';
        append_field(stream, segment);
    }
    stream << '|' << node_id;
    return stream.str();
}

std::vector<std::size_t> find_indexes(
    const std::unordered_map<std::string, std::vector<std::size_t>>& index,
    const std::string& key)
{
    const auto it = index.find(key);
    if (it == index.end()) {
        return {};
    }

    return it->second;
}

} // namespace

void PartialRerunInstructionIndex::record_instruction(
    FunctionExecutionInstructionRecord instruction)
{
    const auto index = instructions_.size();
    by_function_and_node_[function_node_key(instruction.function_id, instruction.node_id)]
        .push_back(index);
    by_call_path_and_node_[call_path_node_key(instruction.call_path, instruction.node_id)]
        .push_back(index);
    instructions_.push_back(std::move(instruction));
}

const std::vector<FunctionExecutionInstructionRecord>&
PartialRerunInstructionIndex::instructions() const noexcept
{
    return instructions_;
}

std::vector<std::size_t> PartialRerunInstructionIndex::find_by_function_and_node(
    const FunctionId& function_id,
    NodeId node_id) const
{
    return find_indexes(by_function_and_node_, function_node_key(function_id, node_id));
}

std::vector<std::size_t> PartialRerunInstructionIndex::find_by_call_path_and_node(
    const FunctionCallPath& call_path,
    NodeId node_id) const
{
    return find_indexes(by_call_path_and_node_, call_path_node_key(call_path, node_id));
}

} // namespace phoenix
