#pragma once

#include "phoenix/partition/unconstrained_solver.hpp"
#include "phoenix/randomness.hpp"

#include <cstddef>
#include <algorithm>
#include <memory>
#include <random>
#include <vector>

namespace phoenix::partition {

inline constexpr std::size_t production_cut_variation_count = 4;

class CompatibilityRandomStream {
public:
    explicit CompatibilityRandomStream(SeedValue item_seed);
    ~CompatibilityRandomStream();
    CompatibilityRandomStream(CompatibilityRandomStream&&) noexcept;
    CompatibilityRandomStream& operator=(CompatibilityRandomStream&&) noexcept;
    CompatibilityRandomStream(const CompatibilityRandomStream&) = delete;
    CompatibilityRandomStream& operator=(const CompatibilityRandomStream&) = delete;

    [[nodiscard]] double next();
    [[nodiscard]] std::vector<CutPointSample> samples(std::size_t count);

    template<class T>
    void shuffle(std::vector<T>& values)
    {
        std::default_random_engine engine{
            static_cast<unsigned long>(next() * 5489U)};
        std::shuffle(values.begin(), values.end(), engine);
    }

private:
    struct State;
    std::unique_ptr<State> state_;
};

[[nodiscard]] std::vector<CutPointSample> generate_compatibility_samples(
    SeedValue item_seed,
    std::size_t variation_count = production_cut_variation_count);

[[nodiscard]] std::vector<CutPointSample> generate_compatibility_samples(
    const SeedDerivationInput& seed_input,
    std::size_t variation_count = production_cut_variation_count);

} // namespace phoenix::partition
