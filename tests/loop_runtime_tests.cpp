#include "phoenix/loop/runtime.hpp"

#include <iostream>

namespace {

struct Trace final : phoenix::loop::TraceSink {
    std::vector<phoenix::loop::TraceEvent> events;
    void record(phoenix::loop::TraceEvent event) override { events.push_back(std::move(event)); }
};

std::int64_t integer(const phoenix::RuntimeValue& value)
{
    const auto* literal = value.as_literal();
    const auto scalar = literal ? phoenix::literal_first_scalar(*literal) : std::nullopt;
    const auto* result = scalar ? std::get_if<std::int64_t>(&*scalar) : nullptr;
    return result ? *result : -1;
}

bool feedback_accumulation_and_index()
{
    phoenix::loop::RunRequest request;
    request.options.count = 4;
    request.input = phoenix::RuntimeValue::literal(std::int64_t{0});
    request.seed = {42, {"root", "loop"}, 7, std::nullopt, std::nullopt};
    std::vector<phoenix::SeedValue> seeds;
    request.body = [&seeds](const phoenix::loop::IterationInput& input) {
        seeds.push_back(input.seed);
        phoenix::loop::IterationResult result;
        result.accumulated = phoenix::RuntimeValue::literal(
            static_cast<std::int64_t>(input.index));
        result.feedback = phoenix::RuntimeValue::literal(integer(input.value) + 1);
        return result;
    };
    const auto result = phoenix::loop::run(request);
    return result.success && result.completed_iterations == 4
        && result.accumulated.size() == 4
        && integer(result.accumulated[3]) == 3
        && seeds.size() == 4 && seeds[0] != seeds[1];
}

bool early_termination()
{
    phoenix::loop::RunRequest request;
    request.options.count = 10;
    request.input = phoenix::RuntimeValue::literal(std::int64_t{1});
    request.body = [](const phoenix::loop::IterationInput& input) {
        phoenix::loop::IterationResult result;
        result.accumulated = phoenix::RuntimeValue::literal(
            static_cast<std::int64_t>(input.index));
        if (input.index < 2) result.feedback = input.value;
        return result;
    };
    const auto result = phoenix::loop::run(request);
    return result.success && result.terminated_early
        && result.completed_iterations == 3 && result.accumulated.size() == 3;
}

bool failure_and_budget_are_transactional()
{
    phoenix::loop::RunRequest failure;
    failure.options.count = 3;
    failure.input = phoenix::RuntimeValue::literal(std::int64_t{1});
    failure.body = [](const phoenix::loop::IterationInput& input) {
        phoenix::loop::IterationResult result;
        result.accumulated = input.value;
        result.feedback = input.value;
        if (input.index == 1) { result.success = false; result.error = "body failed"; }
        return result;
    };
    const auto failed = phoenix::loop::run(failure);
    failure.options.count = 4;
    failure.options.hard_iteration_limit = 3;
    const auto over_budget = phoenix::loop::run(failure);
    return !failed.success && failed.accumulated.empty()
        && failed.failed_iteration == 1
        && !over_budget.success && over_budget.completed_iterations == 0;
}

bool work_budget_and_trace()
{
    phoenix::loop::RunRequest request;
    request.options.count = 4;
    request.options.hard_work_limit = 5;
    request.input = phoenix::RuntimeValue::literal(std::int64_t{1});
    Trace trace;
    request.trace_sink = &trace;
    request.body = [](const phoenix::loop::IterationInput& input) {
        phoenix::loop::IterationResult result;
        result.feedback = input.value;
        result.accumulated = input.value;
        result.work_units = 3;
        return result;
    };
    const auto result = phoenix::loop::run(request);
    return !result.success && result.completed_iterations == 1
        && result.accumulated.empty() && result.work_units == 3
        && result.failed_iteration == 1
        && trace.events.size() == 3
        && trace.events.front().kind == phoenix::loop::TraceEventKind::selected
        && trace.events.back().kind == phoenix::loop::TraceEventKind::failed;
}

} // namespace

int main()
{
    const bool feedback = feedback_accumulation_and_index();
    const bool early = early_termination();
    const bool transactional = failure_and_budget_are_transactional();
    const bool work = work_budget_and_trace();
    std::cout << "loop feedback, accumulation, index, seeds: " << feedback << '\n'
              << "loop early termination: " << early << '\n'
              << "loop transactional failure and budget: " << transactional << '\n'
              << "loop work budget and trace: " << work << '\n';
    return feedback && early && transactional && work ? 0 : 1;
}
