#pragma once

#include "phoenix/actors.hpp"
#include "phoenix/common.hpp"
#include "phoenix/values.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace phoenix {

struct CacheKey {
    std::string stable_key;
};

struct CacheIdentity {
    FunctionId function_id;
    FunctionCallPath call_path;
    std::string graph_revision;
    std::string input_fingerprint;
    SeedValue global_seed = 0;
};

struct InstructionCacheKeyInput {
    CacheIdentity identity;
    NodeId node_id = 0;
    std::optional<SeedValue> effective_seed;
};

struct FunctionCallCacheKeyInput {
    CacheIdentity identity;
};

struct ActorSubtreeCacheKeyInput {
    CacheIdentity identity;
    ActorId actor_id;
};

struct ActorPrototypeCacheKeyInput {
    CacheIdentity identity;
    FunctionId actor_function_id;
    std::string instance_key;
};

class CacheKeyBuilder {
public:
    [[nodiscard]] CacheKey instruction_outputs(const InstructionCacheKeyInput& input) const;
    [[nodiscard]] CacheKey function_call(const FunctionCallCacheKeyInput& input) const;
    [[nodiscard]] CacheKey actor_subtree(const ActorSubtreeCacheKeyInput& input) const;
    [[nodiscard]] CacheKey actor_prototype(const ActorPrototypeCacheKeyInput& input) const;
};

struct InstructionCacheEntry {
    CacheKey key;
    std::vector<PortValue> outputs;
};

struct FunctionCallCacheEntry {
    CacheKey key;
    std::vector<PortValue> outputs;
    std::optional<ActorNode> actor;
};

struct ActorSubtreeCacheEntry {
    CacheKey key;
    ActorNode actor;
};

struct ActorPrototypeCacheEntry {
    CacheKey key;
    ActorNode prototype;
};

class CacheStore {
public:
    [[nodiscard]] virtual ~CacheStore() = default;

    [[nodiscard]] virtual std::optional<InstructionCacheEntry> find_instruction(
        const CacheKey& key) const = 0;
    [[nodiscard]] virtual std::optional<FunctionCallCacheEntry> find_function_call(
        const CacheKey& key) const = 0;
    [[nodiscard]] virtual std::optional<ActorSubtreeCacheEntry> find_actor_subtree(
        const CacheKey& key) const = 0;
    [[nodiscard]] virtual std::optional<ActorPrototypeCacheEntry> find_actor_prototype(
        const CacheKey& key) const = 0;
};

class MemoryCacheStore final : public CacheStore {
public:
    void put_instruction(InstructionCacheEntry entry);
    void put_function_call(FunctionCallCacheEntry entry);
    void put_actor_subtree(ActorSubtreeCacheEntry entry);
    void put_actor_prototype(ActorPrototypeCacheEntry entry);

    [[nodiscard]] bool remove_instruction(const CacheKey& key);
    [[nodiscard]] bool remove_function_call(const CacheKey& key);
    [[nodiscard]] bool remove_actor_subtree(const CacheKey& key);
    [[nodiscard]] bool remove_actor_prototype(const CacheKey& key);
    void clear_identity(const CacheIdentity& identity);
    void clear();

    [[nodiscard]] std::optional<InstructionCacheEntry> find_instruction(
        const CacheKey& key) const override;
    [[nodiscard]] std::optional<FunctionCallCacheEntry> find_function_call(
        const CacheKey& key) const override;
    [[nodiscard]] std::optional<ActorSubtreeCacheEntry> find_actor_subtree(
        const CacheKey& key) const override;
    [[nodiscard]] std::optional<ActorPrototypeCacheEntry> find_actor_prototype(
        const CacheKey& key) const override;

private:
    std::unordered_map<std::string, InstructionCacheEntry> instruction_entries_;
    std::unordered_map<std::string, FunctionCallCacheEntry> function_call_entries_;
    std::unordered_map<std::string, ActorSubtreeCacheEntry> actor_subtree_entries_;
    std::unordered_map<std::string, ActorPrototypeCacheEntry> actor_prototype_entries_;
};

} // namespace phoenix
