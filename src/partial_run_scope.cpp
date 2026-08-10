#include "phoenix/partial_run_scope.hpp"

#include <utility>

namespace phoenix {

PartialRerunScopeResult PartialRerunScopeResolver::resolve(
    const PartialRerunScopeRequest& request) const
{
    if (request.plan == nullptr || request.function == nullptr || !request.actor_id.has_value()) {
        return PartialRerunScopeResult{PartialRerunScopeStatus::invalid_request, std::nullopt};
    }

    if (!request.plan->invalidation.actor_subtree_affected) {
        return PartialRerunScopeResult{PartialRerunScopeStatus::no_actor_subtree_rerun, std::nullopt};
    }

    if (request.plan->actor_subtree_cache_hit) {
        return PartialRerunScopeResult{PartialRerunScopeStatus::cached_subtree_available, std::nullopt};
    }

    ExecutionContext context = request.context;
    if (context.function_id.empty()) {
        context.function_id = request.function->id;
    }

    CallStack call_stack;
    call_stack.push(CallFrame{
        request.function->id,
        context.call_path,
        request.caller_node_id,
        *request.actor_id,
    });

    FunctionExecutionRequest execution_request;
    execution_request.function = request.function;
    execution_request.inputs = request.inputs;
    execution_request.input_defaults = request.input_defaults;
    execution_request.context = std::move(context);
    execution_request.call_stack = std::move(call_stack);

    return PartialRerunScopeResult{
        PartialRerunScopeStatus::resolved,
        std::move(execution_request),
    };
}

const char* to_string(PartialRerunScopeStatus status) noexcept
{
    switch (status) {
    case PartialRerunScopeStatus::resolved:
        return "resolved";
    case PartialRerunScopeStatus::invalid_request:
        return "invalid_request";
    case PartialRerunScopeStatus::no_actor_subtree_rerun:
        return "no_actor_subtree_rerun";
    case PartialRerunScopeStatus::cached_subtree_available:
        return "cached_subtree_available";
    }

    return "unknown";
}

} // namespace phoenix
