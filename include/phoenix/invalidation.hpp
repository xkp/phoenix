#pragma once

#include "phoenix/common.hpp"
#include "phoenix/graph.hpp"

#include <vector>

namespace phoenix {

enum class InvalidationReason {
    instruction_dirty,
    function_outputs_affected,
    actor_subtree_affected,
    parent_propagation_required,
};

struct InvalidationRequest {
    const FunctionDescriptor* function = nullptr;
    std::vector<NodeId> changed_instructions;
};

struct InvalidationResult {
    std::vector<NodeId> dirty_instructions;
    std::vector<InvalidationReason> reasons;
    bool function_outputs_affected = false;
    bool actor_subtree_affected = false;
    bool parent_propagation_required = false;
};

class InvalidationPlanner {
public:
    [[nodiscard]] InvalidationResult plan(const InvalidationRequest& request) const;
};

[[nodiscard]] const char* to_string(InvalidationReason reason) noexcept;

} // namespace phoenix
