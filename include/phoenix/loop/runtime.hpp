#pragma once

#include "phoenix/randomness.hpp"
#include "phoenix/scripting/contract.hpp"
#include "phoenix/values.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace phoenix::loop {

struct Options {
    std::int64_t count = 0;
    std::int64_t range = -1;
    std::int64_t step = -1;
    std::size_t hard_iteration_limit = 10'000;
    std::size_t hard_work_limit = 100'000;
};

struct IterationInput {
    std::size_t index = 0;
    SeedValue seed = 0;
    RuntimeValue value;
    scripting::Bindings variables;
};

struct VariableUpdateResult {
    bool success = true;
    scripting::Bindings variables;
    std::string error;
};

using VariableUpdater = std::function<VariableUpdateResult(
    const scripting::Bindings&, std::size_t, SeedValue)>;

struct IterationResult {
    bool success = true;
    std::optional<RuntimeValue> feedback;
    std::optional<RuntimeValue> accumulated;
    std::string error;
    std::size_t work_units = 1;
};

using Body = std::function<IterationResult(const IterationInput&)>;

enum class TraceEventKind { selected, iteration_completed, terminated_early, failed, completed };

struct TraceEvent {
    TraceEventKind kind = TraceEventKind::selected;
    std::size_t selected_iterations = 0;
    std::size_t completed_iterations = 0;
    std::size_t work_units = 0;
    std::string message;
};

class TraceSink {
public:
    virtual ~TraceSink() = default;
    virtual void record(TraceEvent event) = 0;
};

struct RunRequest {
    Options options;
    RuntimeValue input;
    SeedDerivationInput seed;
    Body body;
    scripting::Bindings initial_variables;
    VariableUpdater update_variables;
    TraceSink* trace_sink = nullptr;
};

struct RunResult {
    bool success = false;
    std::size_t selected_iterations = 0;
    std::size_t completed_iterations = 0;
    std::size_t work_units = 0;
    std::optional<std::size_t> failed_iteration;
    bool terminated_early = false;
    std::vector<RuntimeValue> accumulated;
    std::string error;
};

[[nodiscard]] RunResult run(const RunRequest& request);

} // namespace phoenix::loop
