#include "phoenix/loop/runtime.hpp"

#include <algorithm>
#include <limits>
#include <random>

namespace phoenix::loop {
namespace {

void trace(TraceSink* sink, TraceEvent event)
{
    if (sink) sink->record(std::move(event));
}

std::optional<std::size_t> select_count(const RunRequest& request, std::string& error)
{
    auto minimum = request.options.count;
    auto maximum = request.options.range;
    if (maximum < 0) maximum = minimum;
    if (minimum > maximum) std::swap(minimum, maximum);
    if (maximum <= 0) return std::size_t{0};
    minimum = std::max<std::int64_t>(minimum, 0);

    const auto base_seed = SeedDeriver{}.derive(request.seed);
    std::mt19937_64 engine(base_seed);
    std::int64_t selected = minimum;
    if (request.options.step > 0) {
        const auto slots = (maximum - minimum) / request.options.step;
        std::uniform_int_distribution<std::int64_t> distribution(0, slots);
        selected = minimum + distribution(engine) * request.options.step;
    } else {
        std::uniform_int_distribution<std::int64_t> distribution(minimum, maximum);
        selected = distribution(engine);
    }
    if (selected < 0 || static_cast<std::uint64_t>(selected)
        > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        error = "Loop iteration count is outside the supported range.";
        return std::nullopt;
    }
    return static_cast<std::size_t>(selected);
}

} // namespace

RunResult run(const RunRequest& request)
{
    RunResult result;
    if (!request.body) {
        result.error = "Loop requires a body.";
        return result;
    }
    if (request.input.is_missing()) {
        result.success = true;
        return result;
    }
    const auto count = select_count(request, result.error);
    if (!count) return result;
    result.selected_iterations = *count;
    trace(request.trace_sink, {TraceEventKind::selected, *count, 0, 0, {}});
    if (*count > request.options.hard_iteration_limit) {
        result.error = "Loop iteration count exceeds the hard safety limit.";
        trace(request.trace_sink, {TraceEventKind::failed, *count, 0, 0, result.error});
        return result;
    }
    if (*count == 0) {
        result.success = true;
        trace(request.trace_sink, {TraceEventKind::completed, 0, 0, 0, {}});
        return result;
    }

    RuntimeValue current = request.input;
    auto variables = request.initial_variables;
    for (std::size_t index = 0; index < *count; ++index) {
        auto iteration_seed = request.seed;
        iteration_seed.item_key = index + 1;
        const auto seed = SeedDeriver{}.derive(iteration_seed);
        auto body_variables = variables;
        body_variables["$index"] = static_cast<std::int64_t>(index);
        const IterationInput input{
            index, seed, std::move(current), std::move(body_variables)};
        auto iteration = request.body(input);
        if (!iteration.success) {
            result.failed_iteration = index;
            result.error = iteration.error.empty()
                ? "Loop body failed." : std::move(iteration.error);
            result.accumulated.clear();
            trace(request.trace_sink, {TraceEventKind::failed, *count,
                result.completed_iterations, result.work_units, result.error});
            return result;
        }
        if (iteration.work_units > request.options.hard_work_limit -
                std::min(result.work_units, request.options.hard_work_limit)) {
            result.failed_iteration = index;
            result.error = "Loop body work exceeds the hard safety limit.";
            result.accumulated.clear();
            trace(request.trace_sink, {TraceEventKind::failed, *count,
                result.completed_iterations, result.work_units, result.error});
            return result;
        }
        result.work_units += iteration.work_units;
        ++result.completed_iterations;
        trace(request.trace_sink, {TraceEventKind::iteration_completed, *count,
            result.completed_iterations, result.work_units, {}});
        if (iteration.accumulated && !iteration.accumulated->is_missing()
            && !iteration.accumulated->is_empty())
            result.accumulated.push_back(std::move(*iteration.accumulated));
        if (index + 1 == *count) break;
        if (!iteration.feedback || iteration.feedback->is_missing()
            || iteration.feedback->is_empty()) {
            result.terminated_early = true;
            trace(request.trace_sink, {TraceEventKind::terminated_early, *count,
                result.completed_iterations, result.work_units, {}});
            break;
        }
        current = std::move(*iteration.feedback);
        if (request.update_variables) {
            auto updated = request.update_variables(variables, index + 1, seed);
            if (!updated.success) {
                result.failed_iteration = index + 1;
                result.error = updated.error.empty()
                    ? "Loop variable update failed." : std::move(updated.error);
                result.accumulated.clear();
                trace(request.trace_sink, {TraceEventKind::failed, *count,
                    result.completed_iterations, result.work_units, result.error});
                return result;
            }
            variables = std::move(updated.variables);
        }
    }
    result.success = true;
    trace(request.trace_sink, {TraceEventKind::completed, *count,
        result.completed_iterations, result.work_units, {}});
    return result;
}

} // namespace phoenix::loop
