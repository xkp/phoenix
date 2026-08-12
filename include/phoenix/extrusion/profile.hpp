#pragma once

#include "phoenix/working_geometry.hpp"

#include <CGAL/enum.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace phoenix::extrusion {

struct ProfileSegment {
    double delta_x = 0.0;
    double delta_y = 0.0;
    LabelId face_label = unassigned_label_id;
    LabelId left_label = unassigned_label_id;
    LabelId bottom_label = unassigned_label_id;
    LabelId right_label = unassigned_label_id;
    LabelId top_label = unassigned_label_id;
    LabelId skirt_label = unassigned_label_id;
    bool horizontal = false;
};

class Profile final {
public:
    [[nodiscard]] static std::shared_ptr<const Profile> create(
        std::vector<ProfileSegment> segments);

    [[nodiscard]] std::size_t size() const noexcept { return segments_.size(); }
    [[nodiscard]] const ProfileSegment& segment(std::size_t index) const { return segments_.at(index); }
    [[nodiscard]] CGAL::Sign sign() const noexcept;
    [[nodiscard]] std::pair<double, double> direction(std::size_t index) const;
    [[nodiscard]] double delta(std::size_t index) const { return segment(index).delta_y; }

private:
    explicit Profile(std::vector<ProfileSegment> segments) : segments_(std::move(segments)) {}
    std::vector<ProfileSegment> segments_;
};

using ProfileRef = std::shared_ptr<const Profile>;

struct KernelCornerInput {
    Point3d point;
    ProfileRef profile;
    LabelId cap_label = unassigned_label_id;
    VertexId source_vertex_id;
    HalfedgeId source_halfedge_id;
    EdgeId source_edge_id;
};

struct KernelExtrusionInput {
    std::vector<KernelCornerInput> boundary;
    CGAL::Sign sign = CGAL::ZERO;
    LabelId bottom_label = unassigned_label_id;
    LabelId right_label = unassigned_label_id;
    LabelId top_label = unassigned_label_id;
    LabelId left_label = unassigned_label_id;
    LabelId skirt_label = unassigned_label_id;
    LabelId cap_label = unassigned_label_id;
};

[[nodiscard]] std::optional<KernelExtrusionInput> make_kernel_input(
    const ExtrusionWorkingFace& face,
    ProfileRef profile,
    LabelId bottom_label,
    LabelId right_label,
    LabelId top_label,
    LabelId left_label,
    LabelId skirt_label,
    LabelId cap_label);

} // namespace phoenix::extrusion
