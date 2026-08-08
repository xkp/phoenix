#pragma once

#include "phoenix/common.hpp"
#include "phoenix/values.hpp"

#include <optional>
#include <vector>

namespace phoenix {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Transform {
    Vec3 translation;
    Vec3 rotation_euler;
    Vec3 scale{1.0, 1.0, 1.0};
};

struct ActorPrototypeRef {
    ActorId prototype_id;
};

struct ActorNode {
    ActorId id;
    std::optional<std::string> name;
    Transform transform;
    Vec3 pivot;
    std::optional<GeometryValue> geometry;
    std::vector<ActorNode> children;
    std::optional<ActorPrototypeRef> prototype;
};

struct SceneRoot {
    ActorNode root;
};

} // namespace phoenix
