#pragma once

#include "phoenix/working_geometry.hpp"

#include <optional>
#include <string>
#include <variant>

namespace phoenix::scripting {

enum class HandleKind { geometry, vertex, halfedge, face };

struct GeometryHandle {
    std::uint64_t session = 0;
    std::uint64_t geometry = 0;
};

template <HandleKind Kind>
struct ElementHandle {
    std::uint64_t session = 0;
    std::uint64_t geometry = 0;
    std::uint64_t generation = 0;
    GeometryIndex index = invalid_geometry_index;
};

using VertexHandle = ElementHandle<HandleKind::vertex>;
using HalfedgeHandle = ElementHandle<HandleKind::halfedge>;
using FaceHandle = ElementHandle<HandleKind::face>;

enum class SessionErrorCode {
    invalid_session,
    invalid_geometry,
    stale_handle,
    wrong_element,
    invalid_label,
    invalid_geometry_output,
    session_closed,
};

struct SessionError {
    SessionErrorCode code = SessionErrorCode::invalid_session;
    std::string message;
};

template <typename T>
using SessionResult = std::variant<T, SessionError>;

struct CommitResult {
    std::vector<CanonicalGeometryRef> outputs;
    std::vector<SessionError> errors;
    [[nodiscard]] bool success() const noexcept { return errors.empty(); }
};

class GeometryEditSession {
public:
    explicit GeometryEditSession(std::uint64_t session_id,
        RunElementIdAllocator* ids = nullptr) noexcept;

    [[nodiscard]] SessionResult<GeometryHandle> clone(CanonicalGeometryRef geometry);
    [[nodiscard]] SessionResult<GeometryHandle> create_empty();
    [[nodiscard]] SessionResult<VertexHandle> vertex(GeometryHandle geometry, GeometryIndex index) const;
    [[nodiscard]] SessionResult<HalfedgeHandle> halfedge(GeometryHandle geometry, GeometryIndex index) const;
    [[nodiscard]] SessionResult<FaceHandle> face(GeometryHandle geometry, GeometryIndex index) const;
    [[nodiscard]] SessionResult<std::vector<VertexHandle>> vertices(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<std::vector<HalfedgeHandle>> halfedges(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<std::vector<FaceHandle>> faces(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<std::vector<HalfedgeHandle>> border_halfedges(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<std::size_t> vertex_count(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<std::size_t> halfedge_count(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<std::size_t> face_count(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<std::size_t> border_halfedge_count(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<std::size_t> border_edge_count(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<bool> empty(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<bool> is_closed(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<bool> is_pure_bivalent(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<bool> is_pure_trivalent(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<bool> is_pure_triangle(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<bool> is_pure_quad(GeometryHandle geometry) const;
    [[nodiscard]] SessionResult<Point3d> point(VertexHandle vertex) const;
    [[nodiscard]] SessionResult<LabelId> label(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<LabelId> label(FaceHandle face) const;
    [[nodiscard]] SessionResult<std::uint64_t> element_id(VertexHandle vertex) const;
    [[nodiscard]] SessionResult<std::uint64_t> element_id(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<std::uint64_t> element_id(FaceHandle face) const;
    [[nodiscard]] SessionResult<HalfedgeHandle> halfedge(FaceHandle face) const;
    [[nodiscard]] SessionResult<HalfedgeHandle> halfedge(VertexHandle vertex) const;
    [[nodiscard]] SessionResult<std::vector<HalfedgeHandle>> incident_halfedges(VertexHandle vertex) const;
    [[nodiscard]] SessionResult<std::vector<HalfedgeHandle>> incident_halfedges(FaceHandle face) const;
    [[nodiscard]] SessionResult<std::size_t> degree(VertexHandle vertex) const;
    [[nodiscard]] SessionResult<std::size_t> degree(FaceHandle face) const;
    [[nodiscard]] SessionResult<bool> is_bivalent(VertexHandle vertex) const;
    [[nodiscard]] SessionResult<bool> is_trivalent(VertexHandle vertex) const;
    [[nodiscard]] SessionResult<bool> is_triangle(FaceHandle face) const;
    [[nodiscard]] SessionResult<bool> is_quad(FaceHandle face) const;
    [[nodiscard]] SessionResult<HalfedgeHandle> next(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<HalfedgeHandle> prev(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<HalfedgeHandle> opposite(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<HalfedgeHandle> next_on_vertex(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<HalfedgeHandle> prev_on_vertex(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<VertexHandle> source(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<VertexHandle> target(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<std::optional<FaceHandle>> face(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<bool> is_border(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<bool> is_border_edge(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<std::vector<HalfedgeHandle>> incident_vertex_halfedges(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<std::vector<HalfedgeHandle>> incident_facet_halfedges(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<std::size_t> vertex_degree(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<std::size_t> facet_degree(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<bool> is_bivalent(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<bool> is_trivalent(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<bool> is_triangle(HalfedgeHandle halfedge) const;
    [[nodiscard]] SessionResult<bool> is_quad(HalfedgeHandle halfedge) const;
    [[nodiscard]] std::optional<SessionError> set_point(VertexHandle vertex, Point3d point);
    [[nodiscard]] std::optional<SessionError> set_label(HalfedgeHandle halfedge, LabelId label);
    [[nodiscard]] std::optional<SessionError> set_label(FaceHandle face, LabelId label);
    [[nodiscard]] SessionResult<VertexHandle> add_vertex(GeometryHandle geometry, Point3d point);
    [[nodiscard]] SessionResult<FaceHandle> add_face(GeometryHandle geometry,
        const std::vector<VertexHandle>& vertices,
        LabelId face_label = unassigned_label_id,
        const std::vector<LabelId>& directed_edge_labels = {});
    [[nodiscard]] SessionResult<HalfedgeHandle> split_edge(HalfedgeHandle halfedge);
    [[nodiscard]] SessionResult<HalfedgeHandle> split_facet(
        HalfedgeHandle first, HalfedgeHandle second);
    [[nodiscard]] SessionResult<HalfedgeHandle> join_facet(HalfedgeHandle halfedge);
    [[nodiscard]] SessionResult<HalfedgeHandle> flip_edge(HalfedgeHandle halfedge);
    [[nodiscard]] SessionResult<HalfedgeHandle> make_hole(HalfedgeHandle halfedge);
    [[nodiscard]] SessionResult<HalfedgeHandle> fill_hole(HalfedgeHandle halfedge);
    [[nodiscard]] SessionResult<HalfedgeHandle> split_vertex(
        HalfedgeHandle first, HalfedgeHandle second);
    [[nodiscard]] SessionResult<HalfedgeHandle> join_vertex(HalfedgeHandle halfedge);
    [[nodiscard]] SessionResult<HalfedgeHandle> create_center_vertex(HalfedgeHandle halfedge);
    [[nodiscard]] SessionResult<HalfedgeHandle> erase_center_vertex(HalfedgeHandle halfedge);
    [[nodiscard]] std::optional<SessionError> erase_facet(HalfedgeHandle halfedge);
    [[nodiscard]] std::optional<SessionError> erase_connected_component(HalfedgeHandle halfedge);
    [[nodiscard]] SessionResult<std::size_t> keep_largest_connected_components(
        GeometryHandle geometry,std::size_t count);
    [[nodiscard]] std::optional<SessionError> clear(GeometryHandle geometry);
    [[nodiscard]] std::optional<SessionError> inside_out(GeometryHandle geometry);
    [[nodiscard]] std::optional<SessionError> normalize_border(GeometryHandle geometry);
    [[nodiscard]] SessionResult<HalfedgeHandle> add_vertex_and_facet_to_border(
        HalfedgeHandle first,HalfedgeHandle second);
    [[nodiscard]] SessionResult<HalfedgeHandle> add_facet_to_border(
        HalfedgeHandle first,HalfedgeHandle second);
    [[nodiscard]] CommitResult commit(const std::vector<GeometryHandle>& outputs);
    void rollback() noexcept;
    [[nodiscard]] bool closed() const noexcept { return closed_; }

private:
    struct EditableGeometry {
        CanonicalGeometryRef source;
        std::optional<WorkingGeometry> working;
        std::uint64_t generation = 1;
    };
    [[nodiscard]] std::optional<SessionError> materialize(EditableGeometry& geometry) const;
    void assign_missing_ids(EditableGeometry& geometry);
    [[nodiscard]] EditableGeometry* resolve(GeometryHandle geometry);
    [[nodiscard]] const EditableGeometry* resolve(GeometryHandle geometry) const;
    template <typename Handle>
    [[nodiscard]] const EditableGeometry* resolve_element(Handle handle,
        std::size_t size, SessionError& error) const;

    std::uint64_t session_id_;
    RunElementIdAllocator* ids_;
    std::vector<EditableGeometry> geometries_;
    bool closed_ = false;
};

[[nodiscard]] std::string to_string(SessionErrorCode code);

} // namespace phoenix::scripting
