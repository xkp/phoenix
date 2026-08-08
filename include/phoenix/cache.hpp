#pragma once

#include "phoenix/actors.hpp"
#include "phoenix/common.hpp"
#include "phoenix/values.hpp"

#include <optional>
#include <string>
#include <vector>

namespace phoenix {

struct CacheKey {
    std::string stable_key;
};

struct InstructionCacheEntry {
    CacheKey key;
    std::vector<PortValue> outputs;
};

struct ActorSubtreeCacheEntry {
    CacheKey key;
    ActorNode actor;
};

class CacheStore {
public:
    [[nodiscard]] virtual ~CacheStore() = default;

    [[nodiscard]] virtual std::optional<InstructionCacheEntry> find_instruction(
        const CacheKey& key) const = 0;
    [[nodiscard]] virtual std::optional<ActorSubtreeCacheEntry> find_actor_subtree(
        const CacheKey& key) const = 0;
};

} // namespace phoenix
