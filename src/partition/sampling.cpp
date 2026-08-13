#include "phoenix/partition/sampling.hpp"

#include <boost/random/linear_congruential.hpp>
#include <boost/random/uniform_real_distribution.hpp>

namespace phoenix::partition {

struct CompatibilityRandomStream::State {
    explicit State(SeedValue seed)
        : generator(static_cast<std::uint32_t>(seed)), distribution(0.0, 1.0)
    {
    }

    boost::minstd_rand generator;
    boost::random::uniform_real_distribution<double> distribution;
};

CompatibilityRandomStream::CompatibilityRandomStream(SeedValue item_seed)
    : state_(std::make_unique<State>(item_seed))
{
}

CompatibilityRandomStream::~CompatibilityRandomStream() = default;
CompatibilityRandomStream::CompatibilityRandomStream(CompatibilityRandomStream&&) noexcept = default;
CompatibilityRandomStream& CompatibilityRandomStream::operator=(CompatibilityRandomStream&&) noexcept = default;

double CompatibilityRandomStream::next()
{
    return state_->distribution(state_->generator);
}

std::vector<CutPointSample> CompatibilityRandomStream::samples(std::size_t count)
{
    std::vector<CutPointSample> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        result.push_back({next(), next()});
    return result;
}

std::vector<CutPointSample> generate_compatibility_samples(
    SeedValue item_seed, std::size_t variation_count)
{
    return CompatibilityRandomStream{item_seed}.samples(variation_count);
}

std::vector<CutPointSample> generate_compatibility_samples(
    const SeedDerivationInput& seed_input, std::size_t variation_count)
{
    return generate_compatibility_samples(
        SeedDeriver{}.derive(seed_input), variation_count);
}

} // namespace phoenix::partition
