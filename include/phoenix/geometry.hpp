#pragma once

#include "phoenix/labels.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace phoenix {

using GeometryIndex = std::uint32_t;
inline constexpr GeometryIndex invalid_geometry_index = UINT32_MAX;

template <typename Tag>
class GeometryElementId {
public:
    constexpr GeometryElementId() noexcept = default;
    explicit constexpr GeometryElementId(std::uint64_t value) noexcept : value_(value) {}
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != UINT64_MAX; }
    friend constexpr bool operator==(GeometryElementId left, GeometryElementId right) noexcept
    {
        return left.value_ == right.value_;
    }
    friend constexpr bool operator!=(GeometryElementId left, GeometryElementId right) noexcept
    {
        return !(left == right);
    }
private:
    std::uint64_t value_ = UINT64_MAX;
};

struct VertexIdTag;
struct HalfedgeIdTag;
struct EdgeIdTag;
struct FaceIdTag;
using VertexId = GeometryElementId<VertexIdTag>;
using HalfedgeId = GeometryElementId<HalfedgeIdTag>;
using EdgeId = GeometryElementId<EdgeIdTag>;
using FaceId = GeometryElementId<FaceIdTag>;

struct Point3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

[[nodiscard]] constexpr Point3d point_from_legacy_2d(double x, double z) noexcept
{
    return Point3d{x, 0.0, z};
}

struct RuntimeVertex {
    Point3d point;
    VertexId id;
    GeometryIndex outgoing_halfedge = invalid_geometry_index;
};

struct RuntimeHalfedge {
    GeometryIndex origin_vertex = invalid_geometry_index;
    GeometryIndex face = invalid_geometry_index;
    GeometryIndex next = invalid_geometry_index;
    GeometryIndex previous = invalid_geometry_index;
    GeometryIndex opposite = invalid_geometry_index;
    GeometryIndex radial_next = invalid_geometry_index;
    HalfedgeId id;
    EdgeId edge_id;
    LabelId label = unassigned_label_id;
};

struct RuntimeFace {
    GeometryIndex halfedge = invalid_geometry_index;
    FaceId id;
    LabelId label = unassigned_label_id;
};

enum class GeometryValidationCode {
    non_finite_coordinate,
    invalid_vertex_reference,
    invalid_face_reference,
    invalid_halfedge_reference,
    inconsistent_next_previous,
    inconsistent_opposite,
    inconsistent_radial_cycle,
    broken_face_loop,
    hole_not_supported,
};

struct GeometryValidationIssue {
    GeometryValidationCode code;
    std::string message;
    GeometryIndex element = invalid_geometry_index;
};

struct GeometryValidationResult {
    std::vector<GeometryValidationIssue> issues;
    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
};

class CanonicalGeometry final {
public:
    [[nodiscard]] static std::shared_ptr<const CanonicalGeometry> create(
        std::vector<RuntimeVertex> vertices,
        std::vector<RuntimeHalfedge> halfedges,
        std::vector<RuntimeFace> faces,
        GeometryValidationResult* validation = nullptr);

    [[nodiscard]] const std::vector<RuntimeVertex>& vertices() const noexcept { return vertices_; }
    [[nodiscard]] const std::vector<RuntimeHalfedge>& halfedges() const noexcept { return halfedges_; }
    [[nodiscard]] const std::vector<RuntimeFace>& faces() const noexcept { return faces_; }
    [[nodiscard]] GeometryValidationResult validate() const;
    [[nodiscard]] std::string serialize_canonical() const;
    [[nodiscard]] static std::shared_ptr<const CanonicalGeometry> deserialize_canonical(
        const std::string& serialized,
        GeometryValidationResult* validation = nullptr);
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;

private:
    CanonicalGeometry(
        std::vector<RuntimeVertex> vertices,
        std::vector<RuntimeHalfedge> halfedges,
        std::vector<RuntimeFace> faces);

    std::vector<RuntimeVertex> vertices_;
    std::vector<RuntimeHalfedge> halfedges_;
    std::vector<RuntimeFace> faces_;
};

using CanonicalGeometryRef = std::shared_ptr<const CanonicalGeometry>;

struct FaceReference {
    CanonicalGeometryRef geometry;
    GeometryIndex face_index = invalid_geometry_index;
    FaceId face_id;

    [[nodiscard]] bool valid() const noexcept
    {
        return geometry != nullptr && face_index < geometry->faces().size()
            && geometry->faces()[face_index].id == face_id;
    }
};

struct GeometrySelection {
    std::vector<FaceReference> faces;
};

class RunElementIdAllocator {
public:
    explicit RunElementIdAllocator(std::uint64_t first = 0) noexcept : next_(first) {}
    [[nodiscard]] VertexId next_vertex() noexcept { return VertexId{next_++}; }
    [[nodiscard]] HalfedgeId next_halfedge() noexcept { return HalfedgeId{next_++}; }
    [[nodiscard]] EdgeId next_edge() noexcept { return EdgeId{next_++}; }
    [[nodiscard]] FaceId next_face() noexcept { return FaceId{next_++}; }
private:
    std::uint64_t next_;
};

[[nodiscard]] std::string to_string(GeometryValidationCode code);

} // namespace phoenix
