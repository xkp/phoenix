#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace phoenix {

using NodeId = std::uint64_t;
using PortId = std::string;
using TypeId = std::string;
using FunctionId = std::string;
using ActorId = std::string;
using SeedValue = std::uint64_t;

using FunctionCallPath = std::vector<std::string>;

} // namespace phoenix
