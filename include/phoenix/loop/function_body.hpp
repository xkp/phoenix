#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/loop/runtime.hpp"

namespace phoenix::loop {

struct FunctionBodyPorts {
    PortId input = "input";
    PortId index = "$index";
    PortId feedback = "loop";
    PortId all = "all";
    PortId output = "output";
};

struct FunctionBodyRequest {
    const FunctionExecutor* executor = nullptr;
    const FunctionDescriptor* function = nullptr;
    FunctionExecutionRequest execution;
    FunctionBodyPorts ports;
    NodeId loop_node_id = 0;
    // Non-null only for a loop-owned private ledger. The outer/global ledger
    // must never be passed here; L2 commits one collapsed effect at the end.
    GeometryPublicationLedger* staging_publication_ledger = nullptr;
};

// Creates a bounded-loop body that invokes a complete acyclic Phoenix function
// once per iteration. Geometry publication is deliberately deferred to L2.
[[nodiscard]] Body make_function_body(FunctionBodyRequest request);

} // namespace phoenix::loop
