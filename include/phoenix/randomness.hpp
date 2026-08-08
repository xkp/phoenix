#pragma once

#include "phoenix/common.hpp"

#include <optional>

namespace phoenix {

struct SeedDerivationInput {
    SeedValue global_seed = 0;
    FunctionCallPath call_path;
    NodeId node_id = 0;
    std::optional<SeedValue> local_seed;
    std::optional<std::uint64_t> item_key;
};

enum class MultiplexSeedMode {
    one_seed_for_all,
    one_seed_each,
};

class SeedDeriver {
public:
    [[nodiscard]] SeedValue derive(const SeedDerivationInput& input) const noexcept;
};

} // namespace phoenix
