#pragma once

#include "phoenix/geometry.hpp"

#include <optional>
#include <string>
#include <vector>

namespace phoenix::instance {

enum class OrientationMethod { axis_aligned, by_face_unsupported };
enum class FacePosition { bounding_box_center, centroid };

struct Quaternion {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct PlacementOptions {
    OrientationMethod orientation = OrientationMethod::axis_aligned;
    FacePosition position = FacePosition::bounding_box_center;
    std::optional<LabelId> orientation_label;
    Point3d rotation_degrees;
    Point3d scale{1.0, 1.0, 1.0};
    Point3d translation;
    // Pins the currently observed production defect until migration policy is
    // chosen: extra rotations require a found orientation edge.
    bool preserve_production_rotation_gate = true;
};

struct Placement {
    FaceId source_face_id;
    Point3d origin;
    Quaternion rotation;
    Point3d scale;
};

struct PlacementResult {
    std::vector<Placement> placements;
    std::vector<std::string> diagnostics;
    [[nodiscard]] bool success() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] PlacementResult build_placements(
    const CanonicalGeometry& source, const PlacementOptions& options);
[[nodiscard]] Point3d quaternion_to_euler_xyz(const Quaternion& rotation);

} // namespace phoenix::instance
