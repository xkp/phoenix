#include "phoenix/cache.hpp"

#include <cstdlib>
#include <iostream>

namespace {

phoenix::CacheIdentity base_identity()
{
    phoenix::CacheIdentity identity;
    identity.function_id = "building";
    identity.call_path = {"root", "2:floor"};
    identity.graph_revision = "graph-v1";
    identity.input_fingerprint = "inputs-a";
    identity.global_seed = 42;
    return identity;
}

bool test_same_instruction_identity_produces_same_key()
{
    phoenix::InstructionCacheKeyInput input;
    input.identity = base_identity();
    input.node_id = 7;
    input.effective_seed = 99;

    const phoenix::CacheKeyBuilder builder;
    const auto first = builder.instruction_outputs(input);
    const auto second = builder.instruction_outputs(input);
    return first.stable_key == second.stable_key;
}

bool test_instruction_key_changes_with_node_id()
{
    phoenix::InstructionCacheKeyInput first_input;
    first_input.identity = base_identity();
    first_input.node_id = 7;
    first_input.effective_seed = 99;

    auto second_input = first_input;
    second_input.node_id = 8;

    const phoenix::CacheKeyBuilder builder;
    return builder.instruction_outputs(first_input).stable_key
        != builder.instruction_outputs(second_input).stable_key;
}

bool test_function_key_changes_with_call_path()
{
    phoenix::FunctionCallCacheKeyInput first_input;
    first_input.identity = base_identity();

    auto second_input = first_input;
    second_input.identity.call_path = {"root", "3:floor"};

    const phoenix::CacheKeyBuilder builder;
    return builder.function_call(first_input).stable_key
        != builder.function_call(second_input).stable_key;
}

bool test_function_key_changes_with_seed()
{
    phoenix::FunctionCallCacheKeyInput first_input;
    first_input.identity = base_identity();

    auto second_input = first_input;
    second_input.identity.global_seed = 43;

    const phoenix::CacheKeyBuilder builder;
    return builder.function_call(first_input).stable_key
        != builder.function_call(second_input).stable_key;
}

bool test_function_key_changes_with_input_fingerprint()
{
    phoenix::FunctionCallCacheKeyInput first_input;
    first_input.identity = base_identity();

    auto second_input = first_input;
    second_input.identity.input_fingerprint = "inputs-b";

    const phoenix::CacheKeyBuilder builder;
    return builder.function_call(first_input).stable_key
        != builder.function_call(second_input).stable_key;
}

bool test_actor_subtree_key_changes_with_actor_id()
{
    phoenix::ActorSubtreeCacheKeyInput first_input;
    first_input.identity = base_identity();
    first_input.actor_id = "actor:root";

    auto second_input = first_input;
    second_input.actor_id = "actor:root:2:floor";

    const phoenix::CacheKeyBuilder builder;
    return builder.actor_subtree(first_input).stable_key
        != builder.actor_subtree(second_input).stable_key;
}

bool test_actor_prototype_key_changes_with_instance_key()
{
    phoenix::ActorPrototypeCacheKeyInput first_input;
    first_input.identity = base_identity();
    first_input.actor_function_id = "window";
    first_input.instance_key = "topology-a";

    auto second_input = first_input;
    second_input.instance_key = "topology-b";

    const phoenix::CacheKeyBuilder builder;
    return builder.actor_prototype(first_input).stable_key
        != builder.actor_prototype(second_input).stable_key;
}

bool test_cache_key_kinds_are_distinct()
{
    const auto identity = base_identity();

    phoenix::FunctionCallCacheKeyInput function_input;
    function_input.identity = identity;

    phoenix::ActorPrototypeCacheKeyInput prototype_input;
    prototype_input.identity = identity;
    prototype_input.actor_function_id = "building";
    prototype_input.instance_key = "inputs-a";

    const phoenix::CacheKeyBuilder builder;
    return builder.function_call(function_input).stable_key
        != builder.actor_prototype(prototype_input).stable_key;
}

bool test_missing_instruction_entry_returns_empty()
{
    const phoenix::MemoryCacheStore store;
    const auto missing = store.find_instruction(phoenix::CacheKey{"missing"});
    return !missing.has_value();
}

bool test_stored_instruction_outputs_can_be_retrieved()
{
    phoenix::MemoryCacheStore store;
    phoenix::InstructionCacheEntry entry;
    entry.key = phoenix::CacheKey{"instruction-key"};
    entry.outputs = {
        phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("cached-output")},
    };
    store.put_instruction(entry);

    const auto found = store.find_instruction(entry.key);
    const auto* geometry = found.has_value() && !found->outputs.empty()
        ? found->outputs.front().value.as_geometry()
        : nullptr;
    return geometry != nullptr && geometry->debug_label == "cached-output";
}

bool test_different_instruction_key_does_not_collide()
{
    phoenix::MemoryCacheStore store;
    phoenix::InstructionCacheEntry entry;
    entry.key = phoenix::CacheKey{"instruction-key-a"};
    entry.outputs = {
        phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("cached-output")},
    };
    store.put_instruction(entry);

    return !store.find_instruction(phoenix::CacheKey{"instruction-key-b"}).has_value();
}

bool test_replacing_instruction_entry_updates_value()
{
    phoenix::MemoryCacheStore store;
    phoenix::InstructionCacheEntry first;
    first.key = phoenix::CacheKey{"instruction-key"};
    first.outputs = {
        phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("first")},
    };
    store.put_instruction(first);

    phoenix::InstructionCacheEntry second;
    second.key = first.key;
    second.outputs = {
        phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("second")},
    };
    store.put_instruction(second);

    const auto found = store.find_instruction(first.key);
    const auto* geometry = found.has_value() && !found->outputs.empty()
        ? found->outputs.front().value.as_geometry()
        : nullptr;
    return geometry != nullptr && geometry->debug_label == "second";
}

bool test_stored_actor_subtree_can_be_retrieved()
{
    phoenix::MemoryCacheStore store;
    phoenix::ActorSubtreeCacheEntry entry;
    entry.key = phoenix::CacheKey{"actor-subtree-key"};
    entry.actor.id = "actor:root:child";
    store.put_actor_subtree(entry);

    const auto found = store.find_actor_subtree(entry.key);
    return found.has_value() && found->actor.id == "actor:root:child";
}

bool test_stored_function_call_can_be_retrieved()
{
    phoenix::MemoryCacheStore store;
    phoenix::FunctionCallCacheEntry entry;
    entry.key = phoenix::CacheKey{"function-call-key"};
    entry.outputs = {
        phoenix::PortValue{"result", phoenix::RuntimeValue::geometry("function-result")},
    };
    entry.actor = phoenix::ActorNode{};
    entry.actor->id = "actor:function-call";
    store.put_function_call(entry);

    const auto found = store.find_function_call(entry.key);
    const auto* geometry = found.has_value() && !found->outputs.empty()
        ? found->outputs.front().value.as_geometry()
        : nullptr;

    return geometry != nullptr
        && geometry->debug_label == "function-result"
        && found->actor.has_value()
        && found->actor->id == "actor:function-call";
}

bool test_stored_actor_prototype_can_be_retrieved()
{
    phoenix::MemoryCacheStore store;
    phoenix::ActorPrototypeCacheEntry entry;
    entry.key = phoenix::CacheKey{"actor-prototype-key"};
    entry.prototype.id = "prototype-actor";
    store.put_actor_prototype(entry);

    const auto found = store.find_actor_prototype(entry.key);
    return found.has_value() && found->prototype.id == "prototype-actor";
}

bool test_cache_families_do_not_collide()
{
    phoenix::MemoryCacheStore store;

    phoenix::InstructionCacheEntry instruction;
    instruction.key = phoenix::CacheKey{"same-stable-key"};
    instruction.outputs = {
        phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("instruction")},
    };
    store.put_instruction(instruction);

    phoenix::ActorSubtreeCacheEntry subtree;
    subtree.key = instruction.key;
    subtree.actor.id = "actor";
    store.put_actor_subtree(subtree);

    phoenix::FunctionCallCacheEntry function_call;
    function_call.key = instruction.key;
    function_call.outputs = {
        phoenix::PortValue{"result", phoenix::RuntimeValue::geometry("function")},
    };
    store.put_function_call(function_call);

    const auto found_instruction = store.find_instruction(instruction.key);
    const auto found_function_call = store.find_function_call(function_call.key);
    const auto found_subtree = store.find_actor_subtree(subtree.key);
    const auto* geometry = found_instruction.has_value() && !found_instruction->outputs.empty()
        ? found_instruction->outputs.front().value.as_geometry()
        : nullptr;
    const auto* function_geometry = found_function_call.has_value() && !found_function_call->outputs.empty()
        ? found_function_call->outputs.front().value.as_geometry()
        : nullptr;

    return geometry != nullptr
        && geometry->debug_label == "instruction"
        && function_geometry != nullptr
        && function_geometry->debug_label == "function"
        && found_subtree.has_value()
        && found_subtree->actor.id == "actor";
}

bool test_removing_instruction_entry_does_not_remove_other_families()
{
    phoenix::MemoryCacheStore store;
    const phoenix::CacheKey key{"shared-key"};

    phoenix::InstructionCacheEntry instruction;
    instruction.key = key;
    instruction.outputs = {
        phoenix::PortValue{"output", phoenix::RuntimeValue::geometry("instruction")},
    };
    store.put_instruction(instruction);

    phoenix::ActorSubtreeCacheEntry subtree;
    subtree.key = key;
    subtree.actor.id = "actor";
    store.put_actor_subtree(subtree);

    const bool removed = store.remove_instruction(key);

    return removed
        && !store.find_instruction(key).has_value()
        && store.find_actor_subtree(key).has_value()
        && !store.remove_instruction(key);
}

bool test_removing_each_cache_family()
{
    phoenix::MemoryCacheStore store;

    phoenix::FunctionCallCacheEntry function_call;
    function_call.key = phoenix::CacheKey{"function-call-key"};
    store.put_function_call(function_call);

    phoenix::ActorSubtreeCacheEntry subtree;
    subtree.key = phoenix::CacheKey{"actor-subtree-key"};
    store.put_actor_subtree(subtree);

    phoenix::ActorPrototypeCacheEntry prototype;
    prototype.key = phoenix::CacheKey{"actor-prototype-key"};
    store.put_actor_prototype(prototype);

    return store.remove_function_call(function_call.key)
        && store.remove_actor_subtree(subtree.key)
        && store.remove_actor_prototype(prototype.key)
        && !store.find_function_call(function_call.key).has_value()
        && !store.find_actor_subtree(subtree.key).has_value()
        && !store.find_actor_prototype(prototype.key).has_value();
}

bool test_clear_identity_removes_only_matching_identity_entries()
{
    const phoenix::CacheKeyBuilder builder;
    const auto identity = base_identity();
    auto other_identity = identity;
    other_identity.input_fingerprint = "inputs-b";

    phoenix::InstructionCacheKeyInput instruction_input;
    instruction_input.identity = identity;
    instruction_input.node_id = 7;

    phoenix::InstructionCacheKeyInput other_instruction_input;
    other_instruction_input.identity = other_identity;
    other_instruction_input.node_id = 7;

    phoenix::FunctionCallCacheKeyInput function_input;
    function_input.identity = identity;

    phoenix::ActorSubtreeCacheKeyInput subtree_input;
    subtree_input.identity = identity;
    subtree_input.actor_id = "actor:root:child";

    phoenix::ActorPrototypeCacheKeyInput prototype_input;
    prototype_input.identity = identity;
    prototype_input.actor_function_id = "window";
    prototype_input.instance_key = "same-topology";

    phoenix::MemoryCacheStore store;

    phoenix::InstructionCacheEntry instruction;
    instruction.key = builder.instruction_outputs(instruction_input);
    store.put_instruction(instruction);

    phoenix::InstructionCacheEntry other_instruction;
    other_instruction.key = builder.instruction_outputs(other_instruction_input);
    store.put_instruction(other_instruction);

    phoenix::FunctionCallCacheEntry function_call;
    function_call.key = builder.function_call(function_input);
    store.put_function_call(function_call);

    phoenix::ActorSubtreeCacheEntry subtree;
    subtree.key = builder.actor_subtree(subtree_input);
    store.put_actor_subtree(subtree);

    phoenix::ActorPrototypeCacheEntry prototype;
    prototype.key = builder.actor_prototype(prototype_input);
    store.put_actor_prototype(prototype);

    store.clear_identity(identity);

    return !store.find_instruction(instruction.key).has_value()
        && store.find_instruction(other_instruction.key).has_value()
        && !store.find_function_call(function_call.key).has_value()
        && !store.find_actor_subtree(subtree.key).has_value()
        && !store.find_actor_prototype(prototype.key).has_value();
}

bool test_clear_removes_all_cache_entries()
{
    phoenix::MemoryCacheStore store;

    phoenix::InstructionCacheEntry instruction;
    instruction.key = phoenix::CacheKey{"instruction-key"};
    store.put_instruction(instruction);

    phoenix::FunctionCallCacheEntry function_call;
    function_call.key = phoenix::CacheKey{"function-call-key"};
    store.put_function_call(function_call);

    store.clear();

    return !store.find_instruction(instruction.key).has_value()
        && !store.find_function_call(function_call.key).has_value();
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

    ok = run_test("same instruction identity produces same key", test_same_instruction_identity_produces_same_key) && ok;
    ok = run_test("instruction key changes with node id", test_instruction_key_changes_with_node_id) && ok;
    ok = run_test("function key changes with call path", test_function_key_changes_with_call_path) && ok;
    ok = run_test("function key changes with seed", test_function_key_changes_with_seed) && ok;
    ok = run_test("function key changes with input fingerprint", test_function_key_changes_with_input_fingerprint) && ok;
    ok = run_test("actor subtree key changes with actor id", test_actor_subtree_key_changes_with_actor_id) && ok;
    ok = run_test("actor prototype key changes with instance key", test_actor_prototype_key_changes_with_instance_key) && ok;
    ok = run_test("cache key kinds are distinct", test_cache_key_kinds_are_distinct) && ok;
    ok = run_test("missing instruction entry returns empty", test_missing_instruction_entry_returns_empty) && ok;
    ok = run_test("stored instruction outputs can be retrieved", test_stored_instruction_outputs_can_be_retrieved) && ok;
    ok = run_test("different instruction key does not collide", test_different_instruction_key_does_not_collide) && ok;
    ok = run_test("replacing instruction entry updates value", test_replacing_instruction_entry_updates_value) && ok;
    ok = run_test("stored function call can be retrieved", test_stored_function_call_can_be_retrieved) && ok;
    ok = run_test("stored actor subtree can be retrieved", test_stored_actor_subtree_can_be_retrieved) && ok;
    ok = run_test("stored actor prototype can be retrieved", test_stored_actor_prototype_can_be_retrieved) && ok;
    ok = run_test("cache families do not collide", test_cache_families_do_not_collide) && ok;
    ok = run_test("removing instruction entry does not remove other families", test_removing_instruction_entry_does_not_remove_other_families) && ok;
    ok = run_test("removing each cache family", test_removing_each_cache_family) && ok;
    ok = run_test("clear identity removes only matching identity entries", test_clear_identity_removes_only_matching_identity_entries) && ok;
    ok = run_test("clear removes all cache entries", test_clear_removes_all_cache_entries) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
