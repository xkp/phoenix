#include "phoenix/geometry.hpp"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace phoenix {
namespace {

void add_issue(GeometryValidationResult& result, GeometryValidationCode code,
    GeometryIndex element, std::string message)
{
    result.issues.push_back(GeometryValidationIssue{code, std::move(message), element});
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept
{
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        hash ^= static_cast<unsigned char>((value >> shift) & 0xffU);
        hash *= 1099511628211ULL;
    }
}

std::uint64_t double_bits(double value) noexcept
{
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "Unexpected double size.");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace

CanonicalGeometry::CanonicalGeometry(
    std::vector<RuntimeVertex> vertices,
    std::vector<RuntimeHalfedge> halfedges,
    std::vector<RuntimeFace> faces)
    : vertices_(std::move(vertices)), halfedges_(std::move(halfedges)), faces_(std::move(faces))
{
}

std::shared_ptr<const CanonicalGeometry> CanonicalGeometry::create(
    std::vector<RuntimeVertex> vertices,
    std::vector<RuntimeHalfedge> halfedges,
    std::vector<RuntimeFace> faces,
    GeometryValidationResult* validation)
{
    auto geometry = std::shared_ptr<const CanonicalGeometry>(
        new CanonicalGeometry(std::move(vertices), std::move(halfedges), std::move(faces)));
    auto result = geometry->validate();
    if (validation != nullptr) *validation = result;
    return result.ok() ? geometry : nullptr;
}

GeometryValidationResult CanonicalGeometry::validate() const
{
    GeometryValidationResult result;
    for (GeometryIndex i = 0; i < vertices_.size(); ++i) {
        const auto& vertex = vertices_[i];
        if (!std::isfinite(vertex.point.x) || !std::isfinite(vertex.point.y)
            || !std::isfinite(vertex.point.z)) {
            add_issue(result, GeometryValidationCode::non_finite_coordinate, i,
                "Vertex has a non-finite coordinate.");
        }
        if (vertex.outgoing_halfedge != invalid_geometry_index
            && vertex.outgoing_halfedge >= halfedges_.size()) {
            add_issue(result, GeometryValidationCode::invalid_halfedge_reference, i,
                "Vertex outgoing halfedge is out of range.");
        }
    }

    for (GeometryIndex i = 0; i < halfedges_.size(); ++i) {
        const auto& halfedge = halfedges_[i];
        if (halfedge.origin_vertex >= vertices_.size()) {
            add_issue(result, GeometryValidationCode::invalid_vertex_reference, i,
                "Halfedge origin vertex is out of range.");
        }
        if (halfedge.face >= faces_.size()) {
            add_issue(result, GeometryValidationCode::invalid_face_reference, i,
                "Halfedge face is out of range.");
        }
        if (halfedge.next >= halfedges_.size() || halfedge.previous >= halfedges_.size()) {
            add_issue(result, GeometryValidationCode::invalid_halfedge_reference, i,
                "Halfedge next or previous reference is out of range.");
        } else if (halfedges_[halfedge.next].previous != i
            || halfedges_[halfedge.previous].next != i) {
            add_issue(result, GeometryValidationCode::inconsistent_next_previous, i,
                "Halfedge next/previous links are inconsistent.");
        }
        if (halfedge.opposite != invalid_geometry_index) {
            if (halfedge.opposite >= halfedges_.size()
                || halfedges_[halfedge.opposite].opposite != i) {
                add_issue(result, GeometryValidationCode::inconsistent_opposite, i,
                    "Halfedge opposite link is not reciprocal.");
            }
        }
        if (halfedge.radial_next != invalid_geometry_index
            && (halfedge.radial_next >= halfedges_.size()
                || halfedges_[halfedge.radial_next].edge_id != halfedge.edge_id)) {
            add_issue(result, GeometryValidationCode::inconsistent_radial_cycle, i,
                "Halfedge radial link is invalid or changes topological edge.");
        }
    }

    std::vector<unsigned int> loop_membership(halfedges_.size(), 0);
    for (GeometryIndex face_index = 0; face_index < faces_.size(); ++face_index) {
        const auto start = faces_[face_index].halfedge;
        if (start >= halfedges_.size()) {
            add_issue(result, GeometryValidationCode::invalid_halfedge_reference, face_index,
                "Face representative halfedge is out of range.");
            continue;
        }
        GeometryIndex current = start;
        std::size_t steps = 0;
        do {
            if (current >= halfedges_.size() || halfedges_[current].face != face_index) {
                add_issue(result, GeometryValidationCode::broken_face_loop, face_index,
                    "Face loop leaves the face or references an invalid halfedge.");
                break;
            }
            ++loop_membership[current];
            current = halfedges_[current].next;
            ++steps;
            if (steps > halfedges_.size()) {
                add_issue(result, GeometryValidationCode::broken_face_loop, face_index,
                    "Face loop does not close.");
                break;
            }
        } while (current != start);
    }
    for (GeometryIndex i = 0; i < halfedges_.size(); ++i) {
        if (halfedges_[i].face < faces_.size() && loop_membership[i] == 0) {
            add_issue(result, GeometryValidationCode::hole_not_supported, i,
                "A second boundary cycle on one face would represent a hole.");
        }
    }
    return result;
}

std::string CanonicalGeometry::serialize_canonical() const
{
    std::ostringstream stream;
    stream << "PHOENIX_GEOMETRY_V1\n" << vertices_.size() << ' ' << halfedges_.size()
           << ' ' << faces_.size() << '\n' << std::hexfloat;
    for (const auto& vertex : vertices_) {
        stream << vertex.point.x << ' ' << vertex.point.y << ' ' << vertex.point.z << ' '
               << vertex.id.value() << ' ' << vertex.outgoing_halfedge << '\n';
    }
    for (const auto& halfedge : halfedges_) {
        stream << halfedge.origin_vertex << ' ' << halfedge.face << ' ' << halfedge.next << ' '
               << halfedge.previous << ' ' << halfedge.opposite << ' ' << halfedge.radial_next << ' '
               << halfedge.id.value() << ' ' << halfedge.edge_id.value() << ' '
               << halfedge.label.value() << '\n';
    }
    for (const auto& face : faces_) {
        stream << face.halfedge << ' ' << face.id.value() << ' ' << face.label.value() << '\n';
    }
    return stream.str();
}

std::shared_ptr<const CanonicalGeometry> CanonicalGeometry::deserialize_canonical(
    const std::string& serialized,
    GeometryValidationResult* validation)
{
    std::istringstream stream(serialized);
    std::string header;
    std::getline(stream, header);
    if (header != "PHOENIX_GEOMETRY_V1") return nullptr;

    std::size_t vertex_count = 0;
    std::size_t halfedge_count = 0;
    std::size_t face_count = 0;
    if (!(stream >> vertex_count >> halfedge_count >> face_count)) return nullptr;
    stream >> std::hexfloat;

    std::vector<RuntimeVertex> vertices(vertex_count);
    for (auto& vertex : vertices) {
        std::uint64_t id = UINT64_MAX;
        if (!(stream >> vertex.point.x >> vertex.point.y >> vertex.point.z
              >> id >> vertex.outgoing_halfedge)) return nullptr;
        vertex.id = VertexId{id};
    }

    std::vector<RuntimeHalfedge> halfedges(halfedge_count);
    for (auto& halfedge : halfedges) {
        std::uint64_t id = UINT64_MAX;
        std::uint64_t edge_id = UINT64_MAX;
        std::int32_t label = -1;
        if (!(stream >> halfedge.origin_vertex >> halfedge.face >> halfedge.next
              >> halfedge.previous >> halfedge.opposite >> halfedge.radial_next
              >> id >> edge_id >> label)) return nullptr;
        halfedge.id = HalfedgeId{id};
        halfedge.edge_id = EdgeId{edge_id};
        halfedge.label = LabelId{label};
    }

    std::vector<RuntimeFace> faces(face_count);
    for (auto& face : faces) {
        std::uint64_t id = UINT64_MAX;
        std::int32_t label = -1;
        if (!(stream >> face.halfedge >> id >> label)) return nullptr;
        face.id = FaceId{id};
        face.label = LabelId{label};
    }
    stream >> std::ws;
    if (!stream.eof()) return nullptr;
    return create(std::move(vertices), std::move(halfedges), std::move(faces), validation);
}

std::uint64_t CanonicalGeometry::fingerprint() const noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    hash_u64(hash, vertices_.size());
    hash_u64(hash, halfedges_.size());
    hash_u64(hash, faces_.size());
    for (const auto& vertex : vertices_) {
        hash_u64(hash, double_bits(vertex.point.x));
        hash_u64(hash, double_bits(vertex.point.y));
        hash_u64(hash, double_bits(vertex.point.z));
        hash_u64(hash, vertex.id.value());
        hash_u64(hash, vertex.outgoing_halfedge);
    }
    for (const auto& halfedge : halfedges_) {
        hash_u64(hash, halfedge.origin_vertex);
        hash_u64(hash, halfedge.face);
        hash_u64(hash, halfedge.next);
        hash_u64(hash, halfedge.previous);
        hash_u64(hash, halfedge.opposite);
        hash_u64(hash, halfedge.radial_next);
        hash_u64(hash, halfedge.id.value());
        hash_u64(hash, halfedge.edge_id.value());
        hash_u64(hash, static_cast<std::uint64_t>(halfedge.label.value()));
    }
    for (const auto& face : faces_) {
        hash_u64(hash, face.halfedge);
        hash_u64(hash, face.id.value());
        hash_u64(hash, static_cast<std::uint64_t>(face.label.value()));
    }
    return hash;
}

std::shared_ptr<const CanonicalGeometry> CanonicalGeometry::copy_face(
    GeometryIndex face_index) const
{
    if (face_index >= faces_.size()) return nullptr;
    std::vector<RuntimeVertex> vertices;
    std::vector<RuntimeHalfedge> halfedges;
    std::vector<GeometryIndex> source_loop;
    auto current = faces_[face_index].halfedge;
    do {
        source_loop.push_back(current);
        current = halfedges_[current].next;
    } while (current != faces_[face_index].halfedge);
    vertices.reserve(source_loop.size());
    halfedges.reserve(source_loop.size());
    for (GeometryIndex i = 0; i < source_loop.size(); ++i) {
        const auto& source_halfedge = halfedges_[source_loop[i]];
        auto vertex = vertices_[source_halfedge.origin_vertex];
        vertex.outgoing_halfedge = i;
        vertices.push_back(vertex);
        auto halfedge = source_halfedge;
        halfedge.origin_vertex = i;
        halfedge.face = 0;
        halfedge.next = (i + 1) % static_cast<GeometryIndex>(source_loop.size());
        halfedge.previous = (i + static_cast<GeometryIndex>(source_loop.size()) - 1)
            % static_cast<GeometryIndex>(source_loop.size());
        halfedge.opposite = invalid_geometry_index;
        halfedge.radial_next = invalid_geometry_index;
        halfedges.push_back(halfedge);
    }
    auto face = faces_[face_index];
    face.halfedge = 0;
    return create(std::move(vertices), std::move(halfedges), {face});
}

std::string to_string(GeometryValidationCode code)
{
    switch (code) {
    case GeometryValidationCode::non_finite_coordinate: return "non_finite_coordinate";
    case GeometryValidationCode::invalid_vertex_reference: return "invalid_vertex_reference";
    case GeometryValidationCode::invalid_face_reference: return "invalid_face_reference";
    case GeometryValidationCode::invalid_halfedge_reference: return "invalid_halfedge_reference";
    case GeometryValidationCode::inconsistent_next_previous: return "inconsistent_next_previous";
    case GeometryValidationCode::inconsistent_opposite: return "inconsistent_opposite";
    case GeometryValidationCode::inconsistent_radial_cycle: return "inconsistent_radial_cycle";
    case GeometryValidationCode::broken_face_loop: return "broken_face_loop";
    case GeometryValidationCode::hole_not_supported: return "hole_not_supported";
    }
    return "unknown";
}

} // namespace phoenix
