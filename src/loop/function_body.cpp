#include "phoenix/loop/function_body.hpp"

#include <sstream>
#include <utility>

namespace phoenix::loop {
namespace {

const RuntimeValue* find_output(const FunctionExecutionResult& result, const PortId& port)
{
    for (const auto& output : result.outputs)
        if (output.port == port) return &output.value;
    return nullptr;
}

} // namespace

Body make_function_body(FunctionBodyRequest request)
{
    return [request = std::move(request)](const IterationInput& iteration) {
        IterationResult body;
        if (!request.executor || !request.function) {
            body.success = false;
            body.error = "Loop function body requires an executor and function descriptor.";
            return body;
        }

        auto execution = request.execution;
        execution.function = request.function;
        execution.context.function_id = request.function->id;
        execution.context.global_seed = iteration.seed;
        std::ostringstream segment;
        segment << "loop:" << request.loop_node_id << ":" << iteration.index;
        execution.context.call_path.push_back(segment.str());
        execution.inputs = {
            {request.ports.input, iteration.value},
            {request.ports.index, RuntimeValue::literal(
                static_cast<std::int64_t>(iteration.index))}};

        execution.publication_ledger = request.staging_publication_ledger;
        const auto result = request.executor->run(execution);
        if (result.status != FunctionExecutionStatus::completed
            || result.failure_message || !result.failures.empty()) {
            body.success = false;
            body.error = result.failure_message.value_or(
                result.failures.empty() ? "Loop function body failed."
                                        : result.failures.front().message);
            return body;
        }

        if (const auto* feedback = find_output(result, request.ports.feedback);
            feedback && !feedback->is_missing() && !feedback->is_empty())
            body.feedback = *feedback;
        if (const auto* all = find_output(result, request.ports.all);
            all && !all->is_missing() && !all->is_empty()) {
            body.accumulated = *all;
        } else if (const auto* output = find_output(result, request.ports.output);
            output && !output->is_missing() && !output->is_empty()) {
            body.accumulated = *output;
        }
        return body;
    };
}

} // namespace phoenix::loop
