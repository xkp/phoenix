#include "phoenix/extrusion/profile.hpp"

#include "phoenix/working_geometry.hpp"

#include <cmath>
#include <utility>

namespace phoenix::extrusion {

std::shared_ptr<const Profile> Profile::create(std::vector<ProfileSegment> segments)
{
    if (segments.empty()) return nullptr;
    for (const auto& segment : segments) {
        if (!std::isfinite(segment.delta_x) || !std::isfinite(segment.delta_y)
            || (segment.delta_x == 0.0 && segment.delta_y == 0.0)) return nullptr;
    }
    return std::shared_ptr<const Profile>(new Profile(std::move(segments)));
}

CGAL::Sign Profile::sign() const noexcept
{
    for (const auto& segment : segments_) {
        if (segment.delta_y == 0.0) continue;
        return segment.delta_y > 0.0 ? CGAL::POSITIVE : CGAL::NEGATIVE;
    }
    return CGAL::ZERO;
}

std::pair<double, double> Profile::direction(std::size_t index) const
{
    const auto& value = segments_.at(index);
    return {value.delta_x, value.delta_y};
}

std::optional<KernelExtrusionInput> make_kernel_input(
    const ExtrusionWorkingFace& face,
    ProfileRef profile,
    LabelId bottom_label,
    LabelId right_label,
    LabelId top_label,
    LabelId left_label,
    LabelId skirt_label,
    LabelId cap_label)
{
    if (profile == nullptr || face.boundary.size() < 3 || profile->sign() == CGAL::ZERO) {
        return std::nullopt;
    }
    KernelExtrusionInput input;
    input.sign = profile->sign();
    input.bottom_label = bottom_label;
    input.right_label = right_label;
    input.top_label = top_label;
    input.left_label = left_label;
    input.skirt_label = skirt_label;
    input.cap_label = cap_label;
    input.boundary.reserve(face.boundary.size());
    for (const auto& point : face.boundary) {
        input.boundary.push_back(KernelCornerInput{
            point.point,
            profile,
            cap_label.is_registered() ? cap_label : face.face_label,
            point.source_vertex_id,
            point.source_halfedge_id,
            point.source_edge_id,
        });
    }
    return input;
}

} // namespace phoenix::extrusion
