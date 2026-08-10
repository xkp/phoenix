#include "phoenix/cache.hpp"
#include "phoenix/graph.hpp"
#include "phoenix/partial_run.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using phoenix::EdgeDescriptor;
using phoenix::FunctionDescriptor;
using phoenix::InstructionDescriptor;
using phoenix::PortDescriptor;
using phoenix::PortDirection;

PortDescriptor make_input_port(const char* id, const char* type)
{
    return PortDescriptor{id, type, PortDirection::input};
}

PortDescriptor make_output_port(const char* id, const char* type)
{
    return PortDescriptor{id, type, PortDirection::output};
}

InstructionDescriptor make_instruction(
    phoenix::NodeId id,
    const char* kind,
    std::vector<PortDescriptor> inputs,
    std::vector<PortDescriptor> outputs)
{
    InstructionDescriptor instruction;
    instruction.id = id;
    instruction.kind = kind;
    instruction.input_ports = std::move(inputs);
    instruction.output_ports = std::move(outputs);
    instruction.has_else_port = false;
    return instruction;
}

FunctionDescriptor make_function()
{
    FunctionDescriptor function;
    function.id = "partial";
    function.output_ports = {make_output_port("result", "geometry")};
    function.instructions = {
        make_instruction(
            1,
            "source",
            {},
            {make_output_port("output", "geometry")}),
        make_instruction(
            2,
            "left",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            3,
            "right",
            {make_input_port("input", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            4,
            "join",
            {make_input_port("left", "geometry"), make_input_port("right", "geometry")},
            {make_output_port("output", "geometry")}),
        make_instruction(
            99,
            "output",
            {make_input_port("result", "geometry")},
            {}),
    };
    function.edges = {
        EdgeDescriptor{1, "output", 2, "input"},
        EdgeDescriptor{1, "output", 3, "input"},
        EdgeDescriptor{2, "output", 4, "left"},
        EdgeDescriptor{3, "output", 4, "right"},
        EdgeDescriptor{4, "output", 99, "result"},
    };
    function.output_node_id = 99;
    return function;
}

phoenix::CacheIdentity make_identity()
{
    phoenix::CacheIdentity identity;
    identity.function_id = "partial";
    identity.call_path = {"root"};
    identity.graph_revision = "graph-v1";
    identity.input_fingerprint = "inputs-a";
    identity.global_seed = 42;
    return identity;
}

bool test_partial_plan_includes_dirty_instruction_cache_keys()
{
    const auto function = make_function();
    phoenix::PartialRerunRequest request;
    request.function = &function;
    request.changed_instructions = {2};
    request.cache_identity = make_identity();
    request.effective_instruction_seeds = {
        {2, 20},
        {4, 40},
        {99, 99},
    };

    phoenix::PartialRerunPlanner planner;
    const auto plan = planner.plan(request);

    return plan.instructions.size() == 3
        && plan.instructions[0].node_id == 2
        && plan.instructions[1].node_id == 4
        && plan.instructions[2].node_id == 99
        && !plan.instructions[0].cache_key.stable_key.empty()
        && !plan.instructions[0].cache_hit
        && plan.function_call_key.has_value()
        && !plan.actor_subtree_key.has_value();
}

bool test_partial_plan_detects_instruction_cache_hits()
{
    const auto function = make_function();
    const auto identity = make_identity();
    const phoenix::CacheKeyBuilder key_builder;

    phoenix::MemoryCacheStore cache_store;
    phoenix::InstructionCacheKeyInput key_input;
    key_input.identity = identity;
    key_input.node_id = 4;
    key_input.effective_seed = 40;
    phoenix::InstructionCacheEntry entry;
    entry.key = key_builder.instruction_outputs(key_input);
    entry.outputs = {
        phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("cached")},
    };
    cache_store.put_instruction(entry);

    phoenix::PartialRerunRequest request;
    request.function = &function;
    request.changed_instructions = {2};
    request.cache_identity = identity;
    request.effective_instruction_seeds = {
        {2, 20},
        {4, 40},
        {99, 99},
    };
    request.cache_store = &cache_store;

    phoenix::PartialRerunPlanner planner;
    const auto plan = planner.plan(request);

    return plan.instructions.size() == 3
        && !plan.instructions[0].cache_hit
        && plan.instructions[1].cache_hit
        && !plan.instructions[2].cache_hit;
}

bool test_partial_plan_detects_function_call_cache_hit()
{
    const auto function = make_function();
    const auto identity = make_identity();
    const phoenix::CacheKeyBuilder key_builder;

    phoenix::MemoryCacheStore cache_store;
    phoenix::FunctionCallCacheEntry entry;
    entry.key = key_builder.function_call(phoenix::FunctionCallCacheKeyInput{identity});
    cache_store.put_function_call(entry);

    phoenix::PartialRerunRequest request;
    request.function = &function;
    request.changed_instructions = {2};
    request.cache_identity = identity;
    request.cache_store = &cache_store;

    phoenix::PartialRerunPlanner planner;
    const auto plan = planner.plan(request);

    return plan.function_call_key.has_value()
        && plan.function_call_cache_hit;
}

bool test_partial_plan_detects_actor_subtree_cache_hit()
{
    auto function = make_function();
    function.generates_actor = true;
    const auto identity = make_identity();
    const phoenix::CacheKeyBuilder key_builder;

    phoenix::MemoryCacheStore cache_store;
    phoenix::ActorSubtreeCacheKeyInput key_input;
    key_input.identity = identity;
    key_input.actor_id = "actor:root";
    phoenix::ActorSubtreeCacheEntry entry;
    entry.key = key_builder.actor_subtree(key_input);
    entry.actor.id = "actor:root";
    cache_store.put_actor_subtree(entry);

    phoenix::PartialRerunRequest request;
    request.function = &function;
    request.changed_instructions = {2};
    request.cache_identity = identity;
    request.actor_id = "actor:root";
    request.cache_store = &cache_store;

    phoenix::PartialRerunPlanner planner;
    const auto plan = planner.plan(request);

    return plan.actor_subtree_key.has_value()
        && plan.actor_subtree_cache_hit;
}

bool test_leaf_partial_plan_has_no_function_cache_key()
{
    auto function = make_function();
    function.instructions.push_back(make_instruction(
        50,
        "leaf",
        {},
        {make_output_port("output", "geometry")}));

    phoenix::PartialRerunRequest request;
    request.function = &function;
    request.changed_instructions = {50};
    request.cache_identity = make_identity();

    phoenix::PartialRerunPlanner planner;
    const auto plan = planner.plan(request);

    return plan.instructions.size() == 1
        && plan.instructions.front().node_id == 50
        && !plan.function_call_key.has_value()
        && !plan.invalidation.parent_propagation_required;
}

bool run_test(const char* name, bool (*test_fn)())
{
    const bool passed = test_fn();
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << '\n';
    return passed;
}

} // namespace

int main()
{
    bool ok = true;

    ok = run_test("partial plan includes dirty instruction cache keys", test_partial_plan_includes_dirty_instruction_cache_keys) && ok;
    ok = run_test("partial plan detects instruction cache hits", test_partial_plan_detects_instruction_cache_hits) && ok;
    ok = run_test("partial plan detects function call cache hit", test_partial_plan_detects_function_call_cache_hit) && ok;
    ok = run_test("partial plan detects actor subtree cache hit", test_partial_plan_detects_actor_subtree_cache_hit) && ok;
    ok = run_test("leaf partial plan has no function cache key", test_leaf_partial_plan_has_no_function_cache_key) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
