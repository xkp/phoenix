#pragma once

#include "phoenix/common.hpp"
#include "phoenix/graph.hpp"

#include <vector>

namespace phoenix {

struct InvalidationRequest {
    const FunctionDescriptor* function = nullptr;
    std::vector<NodeId> changed_instructions;
};

struct InvalidationResult {
    std::vector<NodeId> dirty_instructions;
    bool function_outputs_affected = false;
    bool actor_subtree_affected = false;
    bool parent_propagation_required = false;
};

class InvalidationPlanner {
public:
    [[nodiscard]] InvalidationResult plan(const InvalidationRequest& request) const;
};

} // namespace phoenix
