#include "phoenix/cache.hpp"

#include <sstream>
#include <utility>

namespace phoenix {

namespace {

void append_field(std::ostringstream& stream, const std::string& value)
{
    stream << value.size() << ':' << value;
}

void append_number_field(std::ostringstream& stream, std::uint64_t value)
{
    append_field(stream, std::to_string(value));
}

void append_separator(std::ostringstream& stream)
{
    stream << '|';
}

void append_identity(std::ostringstream& stream, const CacheIdentity& identity)
{
    append_field(stream, identity.function_id);
    append_separator(stream);

    append_number_field(stream, static_cast<std::uint64_t>(identity.call_path.size()));
    for (const auto& segment : identity.call_path) {
        append_separator(stream);
        append_field(stream, segment);
    }
    append_separator(stream);

    append_field(stream, identity.graph_revision);
    append_separator(stream);
    append_field(stream, identity.input_fingerprint);
    append_separator(stream);
    append_number_field(stream, identity.global_seed);
}

CacheKey make_key(const char* kind, const CacheIdentity& identity)
{
    std::ostringstream stream;
    append_field(stream, kind);
    append_separator(stream);
    append_identity(stream, identity);
    return CacheKey{stream.str()};
}

bool starts_with(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size()
        && value.compare(0, prefix.size(), prefix) == 0;
}

template <typename Entry>
void remove_entries_with_prefix(
    std::unordered_map<std::string, Entry>& entries,
    const std::string& prefix)
{
    for (auto it = entries.begin(); it != entries.end();) {
        if (starts_with(it->first, prefix)) {
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

CacheKey CacheKeyBuilder::instruction_outputs(const InstructionCacheKeyInput& input) const
{
    auto key = make_key("instruction_outputs", input.identity);
    std::ostringstream stream;
    stream << key.stable_key;
    append_separator(stream);
    append_number_field(stream, input.node_id);
    append_separator(stream);
    append_number_field(stream, input.effective_seed.value_or(0));
    return CacheKey{stream.str()};
}

CacheKey CacheKeyBuilder::function_call(const FunctionCallCacheKeyInput& input) const
{
    return make_key("function_call", input.identity);
}

CacheKey CacheKeyBuilder::actor_subtree(const ActorSubtreeCacheKeyInput& input) const
{
    auto key = make_key("actor_subtree", input.identity);
    std::ostringstream stream;
    stream << key.stable_key;
    append_separator(stream);
    append_field(stream, input.actor_id);
    return CacheKey{stream.str()};
}

CacheKey CacheKeyBuilder::actor_prototype(const ActorPrototypeCacheKeyInput& input) const
{
    auto key = make_key("actor_prototype", input.identity);
    std::ostringstream stream;
    stream << key.stable_key;
    append_separator(stream);
    append_field(stream, input.actor_function_id);
    append_separator(stream);
    append_field(stream, input.instance_key);
    return CacheKey{stream.str()};
}

void MemoryCacheStore::put_instruction(InstructionCacheEntry entry)
{
    instruction_entries_[entry.key.stable_key] = std::move(entry);
}

void MemoryCacheStore::put_function_call(FunctionCallCacheEntry entry)
{
    function_call_entries_[entry.key.stable_key] = std::move(entry);
}

void MemoryCacheStore::put_actor_subtree(ActorSubtreeCacheEntry entry)
{
    actor_subtree_entries_[entry.key.stable_key] = std::move(entry);
}

void MemoryCacheStore::put_actor_prototype(ActorPrototypeCacheEntry entry)
{
    actor_prototype_entries_[entry.key.stable_key] = std::move(entry);
}

bool MemoryCacheStore::remove_instruction(const CacheKey& key)
{
    return instruction_entries_.erase(key.stable_key) > 0;
}

bool MemoryCacheStore::remove_function_call(const CacheKey& key)
{
    return function_call_entries_.erase(key.stable_key) > 0;
}

bool MemoryCacheStore::remove_actor_subtree(const CacheKey& key)
{
    return actor_subtree_entries_.erase(key.stable_key) > 0;
}

bool MemoryCacheStore::remove_actor_prototype(const CacheKey& key)
{
    return actor_prototype_entries_.erase(key.stable_key) > 0;
}

void MemoryCacheStore::clear_identity(const CacheIdentity& identity)
{
    remove_entries_with_prefix(
        instruction_entries_,
        make_key("instruction_outputs", identity).stable_key);
    remove_entries_with_prefix(
        function_call_entries_,
        make_key("function_call", identity).stable_key);
    remove_entries_with_prefix(
        actor_subtree_entries_,
        make_key("actor_subtree", identity).stable_key);
    remove_entries_with_prefix(
        actor_prototype_entries_,
        make_key("actor_prototype", identity).stable_key);
}

void MemoryCacheStore::clear()
{
    instruction_entries_.clear();
    function_call_entries_.clear();
    actor_subtree_entries_.clear();
    actor_prototype_entries_.clear();
}

std::optional<InstructionCacheEntry> MemoryCacheStore::find_instruction(const CacheKey& key) const
{
    const auto it = instruction_entries_.find(key.stable_key);
    if (it == instruction_entries_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::optional<FunctionCallCacheEntry> MemoryCacheStore::find_function_call(const CacheKey& key) const
{
    const auto it = function_call_entries_.find(key.stable_key);
    if (it == function_call_entries_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::optional<ActorSubtreeCacheEntry> MemoryCacheStore::find_actor_subtree(const CacheKey& key) const
{
    const auto it = actor_subtree_entries_.find(key.stable_key);
    if (it == actor_subtree_entries_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::optional<ActorPrototypeCacheEntry> MemoryCacheStore::find_actor_prototype(const CacheKey& key) const
{
    const auto it = actor_prototype_entries_.find(key.stable_key);
    if (it == actor_prototype_entries_.end()) {
        return std::nullopt;
    }

    return it->second;
}

} // namespace phoenix
