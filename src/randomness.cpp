#include "phoenix/randomness.hpp"

#include <functional>

namespace phoenix {

namespace {

inline void hash_combine(std::size_t& seed, std::size_t value) noexcept
{
    seed ^= value + 0x9e3779b9 + (seed << 6U) + (seed >> 2U);
}

} // namespace

SeedValue SeedDeriver::derive(const SeedDerivationInput& input) const noexcept
{
    std::size_t combined = std::hash<SeedValue>{}(input.global_seed);
    hash_combine(combined, std::hash<NodeId>{}(input.node_id));

    for (const auto& segment : input.call_path) {
        hash_combine(combined, std::hash<std::string>{}(segment));
    }

    if (input.local_seed.has_value()) {
        hash_combine(combined, std::hash<SeedValue>{}(*input.local_seed));
    }

    if (input.item_key.has_value()) {
        hash_combine(combined, std::hash<std::uint64_t>{}(*input.item_key));
    }

    return static_cast<SeedValue>(combined);
}

} // namespace phoenix
