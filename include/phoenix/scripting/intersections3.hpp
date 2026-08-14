#pragma once

#include "phoenix/scripting/primitives3.hpp"

#include <variant>

namespace phoenix::scripting {

using Intersection3 = std::variant<std::monostate,Point3d,Line3d,Segment3d,Plane3d>;

[[nodiscard]] Intersection3 intersection(Line3d left,Line3d right);
[[nodiscard]] Intersection3 intersection(Line3d left,Plane3d right);
[[nodiscard]] Intersection3 intersection(Plane3d left,Line3d right);
[[nodiscard]] Intersection3 intersection(Line3d left,Segment3d right);
[[nodiscard]] Intersection3 intersection(Segment3d left,Line3d right);
[[nodiscard]] Intersection3 intersection(Plane3d left,Plane3d right);
[[nodiscard]] Intersection3 intersection(Plane3d left,Segment3d right);
[[nodiscard]] Intersection3 intersection(Segment3d left,Plane3d right);
[[nodiscard]] Intersection3 intersection(Segment3d left,Segment3d right);

} // namespace phoenix::scripting
