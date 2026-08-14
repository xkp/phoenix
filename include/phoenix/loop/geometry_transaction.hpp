#pragma once

#include "phoenix/loop/function_body.hpp"
#include "phoenix/scripting/variables.hpp"

namespace phoenix::loop {

struct GeometryTransactionRequest {
    Options options;
    GeometryValue source;
    ActorId owner_actor_id;
    SeedDerivationInput seed;
    FunctionBodyRequest body;
    std::uint64_t item_key = 0;
    TraceSink* trace_sink = nullptr;
    std::optional<scripting::VariablePlan> variables;
};

struct GeometryTransactionResult {
    RunResult loop;
    GeometryItemEffect publication;
};

// Executes all iterations against a private publication ledger and collapses
// their accumulated geometry into one outer atomic replacement effect.
[[nodiscard]] GeometryTransactionResult run_geometry_transaction(
    const GeometryTransactionRequest& request);

} // namespace phoenix::loop
