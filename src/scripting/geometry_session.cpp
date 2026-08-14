#include "phoenix/scripting/geometry_session.hpp"

#include <CGAL/boost/graph/Euler_operations.h>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace phoenix::scripting {
namespace {
SessionError error(SessionErrorCode code, std::string message)
{
    return {code, std::move(message)};
}
}

GeometryEditSession::GeometryEditSession(std::uint64_t session_id,
    RunElementIdAllocator* ids) noexcept : session_id_(session_id), ids_(ids) {}

SessionResult<GeometryHandle> GeometryEditSession::clone(CanonicalGeometryRef source)
{
    if (closed_) return error(SessionErrorCode::session_closed, "Geometry edit session is closed.");
    if (!source) return error(SessionErrorCode::invalid_geometry, "Cannot clone null geometry.");
    // Promotion is intentionally lazy: scripts that merely pass through or do
    // not inspect this input never pay for a CGAL working mesh.
    geometries_.push_back({std::move(source), std::nullopt, 1});
    return GeometryHandle{session_id_, geometries_.size() - 1};
}

SessionResult<GeometryHandle> GeometryEditSession::create_empty()
{
    if (closed_) return error(SessionErrorCode::session_closed, "Geometry edit session is closed.");
    geometries_.push_back({nullptr, create_working_geometry(), 1});
    return GeometryHandle{session_id_, geometries_.size() - 1};
}

GeometryEditSession::EditableGeometry* GeometryEditSession::resolve(GeometryHandle handle)
{
    if (closed_ || handle.session != session_id_ || handle.geometry >= geometries_.size()) return nullptr;
    auto& geometry = geometries_[static_cast<std::size_t>(handle.geometry)];
    if (materialize(geometry)) return nullptr;
    return &geometry;
}
const GeometryEditSession::EditableGeometry* GeometryEditSession::resolve(GeometryHandle handle) const
{
    if (closed_ || handle.session != session_id_ || handle.geometry >= geometries_.size()) return nullptr;
    auto& geometry = const_cast<EditableGeometry&>(
        geometries_[static_cast<std::size_t>(handle.geometry)]);
    if (materialize(geometry)) return nullptr;
    return &geometry;
}

std::optional<SessionError> GeometryEditSession::materialize(EditableGeometry& geometry) const
{
    if (geometry.working) return {};
    if (!geometry.source)
        return error(SessionErrorCode::invalid_geometry, "Geometry has no canonical source.");
    auto promoted = SurfaceMeshAdapter{}.promote(*geometry.source);
    if (!promoted.success)
        return error(SessionErrorCode::invalid_geometry,
            promoted.diagnostics.empty() ? "CGAL rejected script geometry."
                : promoted.diagnostics.front().message);
    geometry.working = std::move(promoted.working);
    return {};
}

#define HANDLE_GETTER(NAME, TYPE, MEMBER, INDEX) \
SessionResult<TYPE> GeometryEditSession::NAME(GeometryHandle handle, GeometryIndex index) const { \
    const auto* geometry = resolve(handle); \
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle."); \
    const auto descriptor = WorkingSurfaceMesh::INDEX(index); \
    if (!geometry->working->mesh.is_valid(descriptor) || geometry->working->mesh.is_removed(descriptor)) \
        return error(SessionErrorCode::wrong_element, "Element index is out of range."); \
    return TYPE{session_id_, handle.geometry, geometry->generation, index}; \
}
HANDLE_GETTER(vertex, VertexHandle, vertices, Vertex_index)
HANDLE_GETTER(halfedge, HalfedgeHandle, halfedges, Halfedge_index)
HANDLE_GETTER(face, FaceHandle, faces, Face_index)
#undef HANDLE_GETTER

SessionResult<std::vector<VertexHandle>> GeometryEditSession::vertices(GeometryHandle handle) const
{
    const auto* geometry = resolve(handle);
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    std::vector<VertexHandle> result;
    for (const auto value : geometry->working->mesh.vertices()) result.push_back(
        {session_id_,handle.geometry,geometry->generation,static_cast<GeometryIndex>(value.idx())});
    return result;
}
SessionResult<std::vector<HalfedgeHandle>> GeometryEditSession::halfedges(GeometryHandle handle) const
{
    const auto* geometry = resolve(handle);
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    std::vector<HalfedgeHandle> result;
    for (const auto value : geometry->working->mesh.halfedges()) result.push_back(
        {session_id_,handle.geometry,geometry->generation,static_cast<GeometryIndex>(value.idx())});
    return result;
}
SessionResult<std::vector<FaceHandle>> GeometryEditSession::faces(GeometryHandle handle) const
{
    const auto* geometry = resolve(handle);
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    std::vector<FaceHandle> result;
    for (const auto value : geometry->working->mesh.faces()) result.push_back(
        {session_id_,handle.geometry,geometry->generation,static_cast<GeometryIndex>(value.idx())});
    return result;
}
SessionResult<std::vector<HalfedgeHandle>> GeometryEditSession::border_halfedges(GeometryHandle handle) const
{
    const auto* geometry = resolve(handle);
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    std::vector<HalfedgeHandle> result;
    for (const auto value : geometry->working->mesh.halfedges())
        if (geometry->working->mesh.is_border(value)) result.push_back(
            {session_id_,handle.geometry,geometry->generation,static_cast<GeometryIndex>(value.idx())});
    return result;
}

#define GEOMETRY_COUNT(NAME, EXPRESSION) \
SessionResult<std::size_t> GeometryEditSession::NAME(GeometryHandle handle) const { \
    const auto* geometry = resolve(handle); \
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle."); \
    return static_cast<std::size_t>(EXPRESSION); \
}
GEOMETRY_COUNT(vertex_count, std::distance(geometry->working->mesh.vertices().begin(),geometry->working->mesh.vertices().end()))
GEOMETRY_COUNT(halfedge_count, std::distance(geometry->working->mesh.halfedges().begin(),geometry->working->mesh.halfedges().end()))
GEOMETRY_COUNT(face_count, std::distance(geometry->working->mesh.faces().begin(),geometry->working->mesh.faces().end()))
#undef GEOMETRY_COUNT

SessionResult<std::size_t> GeometryEditSession::border_halfedge_count(GeometryHandle handle) const
{
    const auto values = border_halfedges(handle);
    if (const auto* failure = std::get_if<SessionError>(&values)) return *failure;
    return std::get<std::vector<HalfedgeHandle>>(values).size();
}
SessionResult<std::size_t> GeometryEditSession::border_edge_count(GeometryHandle handle) const
{
    const auto* geometry = resolve(handle);
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    std::size_t result=0;
    for (const auto edge : geometry->working->mesh.edges()) {
        const auto h=geometry->working->mesh.halfedge(edge);
        if (geometry->working->mesh.is_border(h)||geometry->working->mesh.is_border(
            geometry->working->mesh.opposite(h))) ++result;
    }
    return result;
}
SessionResult<bool> GeometryEditSession::empty(GeometryHandle handle) const
{
    const auto* geometry=resolve(handle); if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    return geometry->working->mesh.is_empty();
}
SessionResult<bool> GeometryEditSession::is_closed(GeometryHandle handle) const
{
    const auto count=border_halfedge_count(handle); if(const auto* failure=std::get_if<SessionError>(&count))return *failure;
    return std::get<std::size_t>(count)==0;
}

namespace {
template<class Predicate> SessionResult<bool> all_vertices(const GeometryEditSession& session,
    GeometryHandle handle, Predicate predicate)
{
    const auto values=session.vertices(handle); if(const auto* failure=std::get_if<SessionError>(&values))return *failure;
    for(const auto vertex:std::get<std::vector<VertexHandle>>(values)) {
        const auto degree=session.degree(vertex); if(const auto* failure=std::get_if<SessionError>(&degree))return *failure;
        if(!predicate(std::get<std::size_t>(degree)))return false;
    }
    return true;
}
template<class Predicate> SessionResult<bool> all_faces(const GeometryEditSession& session,
    GeometryHandle handle, Predicate predicate)
{
    const auto values=session.faces(handle); if(const auto* failure=std::get_if<SessionError>(&values))return *failure;
    for(const auto face:std::get<std::vector<FaceHandle>>(values)) {
        const auto degree=session.degree(face); if(const auto* failure=std::get_if<SessionError>(&degree))return *failure;
        if(!predicate(std::get<std::size_t>(degree)))return false;
    }
    return true;
}
}
SessionResult<bool> GeometryEditSession::is_pure_bivalent(GeometryHandle h) const{return all_vertices(*this,h,[](auto n){return n==2;});}
SessionResult<bool> GeometryEditSession::is_pure_trivalent(GeometryHandle h) const{return all_vertices(*this,h,[](auto n){return n==3;});}
SessionResult<bool> GeometryEditSession::is_pure_triangle(GeometryHandle h) const{return all_faces(*this,h,[](auto n){return n==3;});}
SessionResult<bool> GeometryEditSession::is_pure_quad(GeometryHandle h) const{return all_faces(*this,h,[](auto n){return n==4;});}

template <typename Handle>
const GeometryEditSession::EditableGeometry* GeometryEditSession::resolve_element(
    Handle handle, std::size_t, SessionError& out) const
{
    const auto* geometry = resolve({handle.session, handle.geometry});
    if (!geometry) { out = error(SessionErrorCode::invalid_geometry, "Invalid geometry handle."); return nullptr; }
    if (handle.generation != geometry->generation) { out = error(SessionErrorCode::stale_handle, "Topology handle is stale."); return nullptr; }
    const auto& mesh = geometry->working->mesh;
    bool valid = false;
    if constexpr (std::is_same_v<Handle, VertexHandle>) {
        const auto descriptor = WorkingSurfaceMesh::Vertex_index(handle.index);
        valid = mesh.is_valid(descriptor) && !mesh.is_removed(descriptor);
    } else if constexpr (std::is_same_v<Handle, HalfedgeHandle>) {
        const auto descriptor = WorkingSurfaceMesh::Halfedge_index(handle.index);
        valid = mesh.is_valid(descriptor) && !mesh.is_removed(descriptor);
    } else if constexpr (std::is_same_v<Handle, FaceHandle>) {
        const auto descriptor = WorkingSurfaceMesh::Face_index(handle.index);
        valid = mesh.is_valid(descriptor) && !mesh.is_removed(descriptor);
    }
    if (!valid) { out = error(SessionErrorCode::wrong_element, "Element index is out of range or removed."); return nullptr; }
    return geometry;
}

SessionResult<Point3d> GeometryEditSession::point(VertexHandle handle) const
{
    SessionError failure; const auto* geometry = resolve({handle.session, handle.geometry});
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    geometry = resolve_element(handle, geometry->working->mesh.num_vertices(), failure);
    if (!geometry) return failure;
    const auto& value = geometry->working->mesh.point(WorkingSurfaceMesh::Vertex_index(handle.index));
    return Point3d{CGAL::to_double(value.x()),CGAL::to_double(value.y()),CGAL::to_double(value.z())};
}
SessionResult<LabelId> GeometryEditSession::label(HalfedgeHandle handle) const
{
    SessionError failure; const auto* geometry = resolve({handle.session, handle.geometry});
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    geometry = resolve_element(handle, geometry->working->mesh.num_halfedges(), failure);
    return geometry ? SessionResult<LabelId>{LabelId{geometry->working->halfedge_labels[WorkingSurfaceMesh::Halfedge_index(handle.index)]}} : SessionResult<LabelId>{failure};
}
SessionResult<LabelId> GeometryEditSession::label(FaceHandle handle) const
{
    SessionError failure; const auto* geometry = resolve({handle.session, handle.geometry});
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    geometry = resolve_element(handle, geometry->working->mesh.num_faces(), failure);
    return geometry ? SessionResult<LabelId>{LabelId{geometry->working->face_labels[WorkingSurfaceMesh::Face_index(handle.index)]}} : SessionResult<LabelId>{failure};
}

#define ELEMENT_ID(TYPE,COUNT,DESCRIPTOR,MAP) \
SessionResult<std::uint64_t> GeometryEditSession::element_id(TYPE handle) const { \
    SessionError failure;const auto* geometry=resolve({handle.session,handle.geometry}); \
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle."); \
    geometry=resolve_element(handle,geometry->working->mesh.COUNT(),failure);if(!geometry)return failure; \
    return geometry->working->MAP[WorkingSurfaceMesh::DESCRIPTOR(handle.index)]; \
}
ELEMENT_ID(VertexHandle,num_vertices,Vertex_index,vertex_ids)
ELEMENT_ID(HalfedgeHandle,num_halfedges,Halfedge_index,halfedge_ids)
ELEMENT_ID(FaceHandle,num_faces,Face_index,face_ids)
#undef ELEMENT_ID

SessionResult<HalfedgeHandle> GeometryEditSession::halfedge(FaceHandle handle) const
{
    SessionError failure; const auto* geometry = resolve({handle.session, handle.geometry});
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    geometry = resolve_element(handle, geometry->working->mesh.num_faces(), failure);
    if (!geometry) return failure;
    const auto value = geometry->working->mesh.halfedge(WorkingSurfaceMesh::Face_index(handle.index));
    if (value == WorkingSurfaceMesh::null_halfedge())
        return error(SessionErrorCode::wrong_element, "Face has no boundary halfedge.");
    return HalfedgeHandle{session_id_, handle.geometry, geometry->generation,
        static_cast<GeometryIndex>(value.idx())};
}

SessionResult<HalfedgeHandle> GeometryEditSession::halfedge(VertexHandle handle) const
{
    SessionError failure; const auto* geometry=resolve({handle.session,handle.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    geometry=resolve_element(handle,geometry->working->mesh.num_vertices(),failure); if(!geometry)return failure;
    const auto value=geometry->working->mesh.halfedge(WorkingSurfaceMesh::Vertex_index(handle.index));
    if(value==WorkingSurfaceMesh::null_halfedge())return error(SessionErrorCode::wrong_element,"Vertex is isolated.");
    return HalfedgeHandle{session_id_,handle.geometry,geometry->generation,static_cast<GeometryIndex>(value.idx())};
}

SessionResult<std::vector<HalfedgeHandle>> GeometryEditSession::incident_halfedges(VertexHandle handle) const
{
    const auto first=halfedge(handle); if(const auto* failure=std::get_if<SessionError>(&first))return *failure;
    std::vector<HalfedgeHandle> result; auto current=std::get<HalfedgeHandle>(first);
    do { result.push_back(current); const auto following=next_on_vertex(current);
        if(const auto* failure=std::get_if<SessionError>(&following))return *failure;
        current=std::get<HalfedgeHandle>(following);
    } while(current.index!=std::get<HalfedgeHandle>(first).index);
    return result;
}

SessionResult<std::vector<HalfedgeHandle>> GeometryEditSession::incident_halfedges(FaceHandle handle) const
{
    const auto first=halfedge(handle); if(const auto* failure=std::get_if<SessionError>(&first))return *failure;
    std::vector<HalfedgeHandle> result; auto current=std::get<HalfedgeHandle>(first);
    do { result.push_back(current); const auto following=next(current);
        if(const auto* failure=std::get_if<SessionError>(&following))return *failure;
        current=std::get<HalfedgeHandle>(following);
    } while(current.index!=std::get<HalfedgeHandle>(first).index);
    return result;
}

SessionResult<std::size_t> GeometryEditSession::degree(VertexHandle handle) const
{
    const auto values=incident_halfedges(handle); if(const auto* failure=std::get_if<SessionError>(&values))return *failure;
    return std::get<std::vector<HalfedgeHandle>>(values).size();
}
SessionResult<std::size_t> GeometryEditSession::degree(FaceHandle handle) const
{
    const auto values=incident_halfedges(handle); if(const auto* failure=std::get_if<SessionError>(&values))return *failure;
    return std::get<std::vector<HalfedgeHandle>>(values).size();
}
SessionResult<bool> GeometryEditSession::is_bivalent(VertexHandle h) const {const auto n=degree(h);if(const auto* e=std::get_if<SessionError>(&n))return *e;return std::get<std::size_t>(n)==2;}
SessionResult<bool> GeometryEditSession::is_trivalent(VertexHandle h) const {const auto n=degree(h);if(const auto* e=std::get_if<SessionError>(&n))return *e;return std::get<std::size_t>(n)==3;}
SessionResult<bool> GeometryEditSession::is_triangle(FaceHandle h) const {const auto n=degree(h);if(const auto* e=std::get_if<SessionError>(&n))return *e;return std::get<std::size_t>(n)==3;}
SessionResult<bool> GeometryEditSession::is_quad(FaceHandle h) const {const auto n=degree(h);if(const auto* e=std::get_if<SessionError>(&n))return *e;return std::get<std::size_t>(n)==4;}

SessionResult<HalfedgeHandle> GeometryEditSession::next(HalfedgeHandle handle) const
{
    SessionError failure; const auto* geometry = resolve({handle.session, handle.geometry});
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    geometry = resolve_element(handle, geometry->working->mesh.num_halfedges(), failure);
    if (!geometry) return failure;
    const auto value = geometry->working->mesh.next(WorkingSurfaceMesh::Halfedge_index(handle.index));
    return HalfedgeHandle{session_id_, handle.geometry, geometry->generation,
        static_cast<GeometryIndex>(value.idx())};
}

SessionResult<HalfedgeHandle> GeometryEditSession::prev(HalfedgeHandle handle) const
{
    SessionError failure; const auto* geometry=resolve({handle.session,handle.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    geometry=resolve_element(handle,geometry->working->mesh.num_halfedges(),failure); if(!geometry)return failure;
    const auto value=geometry->working->mesh.prev(WorkingSurfaceMesh::Halfedge_index(handle.index));
    return HalfedgeHandle{session_id_,handle.geometry,geometry->generation,static_cast<GeometryIndex>(value.idx())};
}

SessionResult<HalfedgeHandle> GeometryEditSession::opposite(HalfedgeHandle handle) const
{
    SessionError failure; const auto* geometry = resolve({handle.session, handle.geometry});
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    geometry = resolve_element(handle, geometry->working->mesh.num_halfedges(), failure);
    if (!geometry) return failure;
    const auto value = geometry->working->mesh.opposite(WorkingSurfaceMesh::Halfedge_index(handle.index));
    return HalfedgeHandle{session_id_, handle.geometry, geometry->generation,
        static_cast<GeometryIndex>(value.idx())};
}

SessionResult<HalfedgeHandle> GeometryEditSession::next_on_vertex(HalfedgeHandle handle) const
{
    const auto following=next(handle); if(const auto* failure=std::get_if<SessionError>(&following))return *failure;
    return opposite(std::get<HalfedgeHandle>(following));
}
SessionResult<HalfedgeHandle> GeometryEditSession::prev_on_vertex(HalfedgeHandle handle) const
{
    const auto opposing=opposite(handle); if(const auto* failure=std::get_if<SessionError>(&opposing))return *failure;
    return prev(std::get<HalfedgeHandle>(opposing));
}

#define HALFEDGE_VERTEX(NAME, ACCESSOR) \
SessionResult<VertexHandle> GeometryEditSession::NAME(HalfedgeHandle handle) const { \
    SessionError failure; const auto* geometry=resolve({handle.session,handle.geometry}); \
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle."); \
    geometry=resolve_element(handle,geometry->working->mesh.num_halfedges(),failure); if(!geometry)return failure; \
    const auto value=geometry->working->mesh.ACCESSOR(WorkingSurfaceMesh::Halfedge_index(handle.index)); \
    return VertexHandle{session_id_,handle.geometry,geometry->generation,static_cast<GeometryIndex>(value.idx())}; \
}
HALFEDGE_VERTEX(source, source)
HALFEDGE_VERTEX(target, target)
#undef HALFEDGE_VERTEX

SessionResult<std::optional<FaceHandle>> GeometryEditSession::face(HalfedgeHandle handle) const
{
    SessionError failure; const auto* geometry=resolve({handle.session,handle.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    geometry=resolve_element(handle,geometry->working->mesh.num_halfedges(),failure); if(!geometry)return failure;
    const auto value=geometry->working->mesh.face(WorkingSurfaceMesh::Halfedge_index(handle.index));
    if(value==WorkingSurfaceMesh::null_face())return std::optional<FaceHandle>{};
    return std::optional<FaceHandle>{{session_id_,handle.geometry,geometry->generation,static_cast<GeometryIndex>(value.idx())}};
}
SessionResult<bool> GeometryEditSession::is_border(HalfedgeHandle handle) const
{
    const auto value=face(handle); if(const auto* failure=std::get_if<SessionError>(&value))return *failure;
    return !std::get<std::optional<FaceHandle>>(value).has_value();
}
SessionResult<bool> GeometryEditSession::is_border_edge(HalfedgeHandle handle) const
{
    const auto first=is_border(handle); if(const auto* failure=std::get_if<SessionError>(&first))return *failure;
    const auto opposing=opposite(handle); if(const auto* failure=std::get_if<SessionError>(&opposing))return *failure;
    const auto second=is_border(std::get<HalfedgeHandle>(opposing)); if(const auto* failure=std::get_if<SessionError>(&second))return *failure;
    return std::get<bool>(first)||std::get<bool>(second);
}
SessionResult<std::vector<HalfedgeHandle>> GeometryEditSession::incident_vertex_halfedges(HalfedgeHandle handle) const
{
    const auto value=target(handle);if(const auto* failure=std::get_if<SessionError>(&value))return *failure;
    return incident_halfedges(std::get<VertexHandle>(value));
}
SessionResult<std::vector<HalfedgeHandle>> GeometryEditSession::incident_facet_halfedges(HalfedgeHandle handle) const
{
    const auto value=face(handle);if(const auto* failure=std::get_if<SessionError>(&value))return *failure;
    const auto& result=std::get<std::optional<FaceHandle>>(value);
    if(!result)return error(SessionErrorCode::wrong_element,"Border halfedge has no incident face.");
    return incident_halfedges(*result);
}
SessionResult<std::size_t> GeometryEditSession::vertex_degree(HalfedgeHandle handle) const
{
    const auto value=target(handle); if(const auto* failure=std::get_if<SessionError>(&value))return *failure;
    return degree(std::get<VertexHandle>(value));
}
SessionResult<std::size_t> GeometryEditSession::facet_degree(HalfedgeHandle handle) const
{
    const auto value=face(handle); if(const auto* failure=std::get_if<SessionError>(&value))return *failure;
    const auto& result=std::get<std::optional<FaceHandle>>(value);
    if(!result)return error(SessionErrorCode::wrong_element,"Border halfedge has no incident face.");
    return degree(*result);
}
SessionResult<bool> GeometryEditSession::is_bivalent(HalfedgeHandle h) const {const auto n=vertex_degree(h);if(const auto* e=std::get_if<SessionError>(&n))return *e;return std::get<std::size_t>(n)==2;}
SessionResult<bool> GeometryEditSession::is_trivalent(HalfedgeHandle h) const {const auto n=vertex_degree(h);if(const auto* e=std::get_if<SessionError>(&n))return *e;return std::get<std::size_t>(n)==3;}
SessionResult<bool> GeometryEditSession::is_triangle(HalfedgeHandle h) const {const auto n=facet_degree(h);if(const auto* e=std::get_if<SessionError>(&n))return *e;return std::get<std::size_t>(n)==3;}
SessionResult<bool> GeometryEditSession::is_quad(HalfedgeHandle h) const {const auto n=facet_degree(h);if(const auto* e=std::get_if<SessionError>(&n))return *e;return std::get<std::size_t>(n)==4;}

std::optional<SessionError> GeometryEditSession::set_point(VertexHandle handle, Point3d value)
{
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
        return error(SessionErrorCode::invalid_geometry_output, "Vertex point is not finite.");
    SessionError failure; auto* geometry = resolve({handle.session, handle.geometry});
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    if (!resolve_element(handle, geometry->working->mesh.num_vertices(), failure)) return failure;
    geometry->working->mesh.point(WorkingSurfaceMesh::Vertex_index(handle.index)) = WorkingPoint{value.x,value.y,value.z}; return {};
}
std::optional<SessionError> GeometryEditSession::set_label(HalfedgeHandle handle, LabelId value)
{
    if (value.value() < unassigned_label_id.value()) return error(SessionErrorCode::invalid_label, "Halfedge label is invalid.");
    SessionError failure; auto* geometry = resolve({handle.session, handle.geometry});
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    if (!resolve_element(handle, geometry->working->mesh.num_halfedges(), failure)) return failure;
    geometry->working->halfedge_labels[WorkingSurfaceMesh::Halfedge_index(handle.index)] = value.value(); return {};
}
std::optional<SessionError> GeometryEditSession::set_label(FaceHandle handle, LabelId value)
{
    if (value.value() < unassigned_label_id.value()) return error(SessionErrorCode::invalid_label, "Face label is invalid.");
    SessionError failure; auto* geometry = resolve({handle.session, handle.geometry});
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    if (!resolve_element(handle, geometry->working->mesh.num_faces(), failure)) return failure;
    geometry->working->face_labels[WorkingSurfaceMesh::Face_index(handle.index)] = value.value(); return {};
}

SessionResult<VertexHandle> GeometryEditSession::add_vertex(GeometryHandle handle, Point3d value)
{
    auto* geometry = resolve(handle);
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
        return error(SessionErrorCode::invalid_geometry_output, "Vertex point is not finite.");
    const auto vertex = geometry->working->mesh.add_vertex(WorkingPoint{value.x,value.y,value.z});
    geometry->working->vertex_ids[vertex] = ids_ ? ids_->next_vertex().value() : UINT64_MAX;
    geometry->working->source_vertices[vertex] = invalid_geometry_index;
    return VertexHandle{session_id_, handle.geometry, geometry->generation,
        static_cast<GeometryIndex>(vertex.idx())};
}

SessionResult<FaceHandle> GeometryEditSession::add_face(GeometryHandle handle,
    const std::vector<VertexHandle>& vertices, LabelId face_label,
    const std::vector<LabelId>& directed_labels)
{
    auto* geometry = resolve(handle);
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    if (vertices.size() < 3)
        return error(SessionErrorCode::invalid_geometry_output, "A face requires at least three vertices.");
    if (!directed_labels.empty() && directed_labels.size() != vertices.size())
        return error(SessionErrorCode::invalid_label, "Directed-edge label count must match the face degree.");
    if (face_label.value() < unassigned_label_id.value())
        return error(SessionErrorCode::invalid_label, "Face label is invalid.");

    std::vector<WorkingSurfaceMesh::Vertex_index> loop;
    loop.reserve(vertices.size());
    for (const auto vertex_handle : vertices) {
        if (vertex_handle.session != session_id_ || vertex_handle.geometry != handle.geometry
            || vertex_handle.generation != geometry->generation)
            return error(SessionErrorCode::stale_handle,
                "Face builder received a stale or foreign vertex handle.");
        const auto vertex = WorkingSurfaceMesh::Vertex_index(vertex_handle.index);
        if (!geometry->working->mesh.is_valid(vertex)
            || geometry->working->mesh.is_removed(vertex))
            return error(SessionErrorCode::wrong_element, "Face vertex is invalid.");
        loop.push_back(vertex);
    }

    const auto face = geometry->working->mesh.add_face(loop);
    if (face == WorkingSurfaceMesh::null_face())
        return error(SessionErrorCode::invalid_geometry_output,
            "CGAL rejected the face because it duplicates/conflicts with topology or is non-manifold.");

    geometry->working->face_ids[face] = ids_ ? ids_->next_face().value() : UINT64_MAX;
    geometry->working->face_labels[face] = face_label.value();
    geometry->working->source_faces[face] = invalid_geometry_index;

    auto current = geometry->working->mesh.halfedge(face);
    do {
        const auto source = geometry->working->mesh.source(current);
        const auto found = std::find(loop.begin(), loop.end(), source);
        const auto label_index = static_cast<std::size_t>(std::distance(loop.begin(), found));
        if (geometry->working->halfedge_ids[current] == UINT64_MAX)
            geometry->working->halfedge_ids[current] = ids_ ? ids_->next_halfedge().value() : UINT64_MAX;
        const auto opposite = geometry->working->mesh.opposite(current);
        auto edge_id = geometry->working->edge_ids[opposite];
        if (edge_id == UINT64_MAX) edge_id = ids_ ? ids_->next_edge().value() : UINT64_MAX;
        geometry->working->edge_ids[current] = edge_id;
        geometry->working->edge_ids[opposite] = edge_id;
        geometry->working->halfedge_labels[current] = directed_labels.empty()
            ? unassigned_label_id.value() : directed_labels[label_index].value();
        geometry->working->source_halfedges[current] = invalid_geometry_index;
        current = geometry->working->mesh.next(current);
    } while (current != geometry->working->mesh.halfedge(face));

    ++geometry->generation;
    return FaceHandle{session_id_, handle.geometry, geometry->generation,
        static_cast<GeometryIndex>(face.idx())};
}

void GeometryEditSession::assign_missing_ids(EditableGeometry& geometry)
{
    auto& working = *geometry.working;
    for (const auto vertex : working.mesh.vertices())
        if (working.vertex_ids[vertex] == UINT64_MAX && ids_)
            working.vertex_ids[vertex] = ids_->next_vertex().value();
    for (const auto halfedge : working.mesh.halfedges())
        if (working.halfedge_ids[halfedge] == UINT64_MAX && ids_)
            working.halfedge_ids[halfedge] = ids_->next_halfedge().value();
    for (const auto edge : working.mesh.edges()) {
        const auto halfedge = working.mesh.halfedge(edge);
        const auto opposite = working.mesh.opposite(halfedge);
        auto id = working.edge_ids[halfedge];
        if (id == UINT64_MAX) id = working.edge_ids[opposite];
        if (id == UINT64_MAX && ids_) id = ids_->next_edge().value();
        working.edge_ids[halfedge] = id;
        working.edge_ids[opposite] = id;
    }
    for (const auto face : working.mesh.faces())
        if (working.face_ids[face] == UINT64_MAX && ids_)
            working.face_ids[face] = ids_->next_face().value();
}

SessionResult<HalfedgeHandle> GeometryEditSession::split_edge(HalfedgeHandle handle)
{
    auto* geometry = resolve({handle.session, handle.geometry});
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    SessionError failure;
    if (!resolve_element(handle, geometry->working->mesh.num_halfedges(), failure)) return failure;
    const auto halfedge = WorkingSurfaceMesh::Halfedge_index(handle.index);
    const auto created = CGAL::Euler::split_edge(halfedge, geometry->working->mesh);
    assign_missing_ids(*geometry);
    ++geometry->generation;
    return HalfedgeHandle{session_id_, handle.geometry, geometry->generation,
        static_cast<GeometryIndex>(created.idx())};
}

SessionResult<HalfedgeHandle> GeometryEditSession::split_facet(
    HalfedgeHandle first, HalfedgeHandle second)
{
    if (first.session != second.session || first.geometry != second.geometry
        || first.generation != second.generation)
        return error(SessionErrorCode::stale_handle, "Facet split handles do not share one generation.");
    auto* geometry = resolve({first.session, first.geometry});
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    SessionError failure;
    if (!resolve_element(first, geometry->working->mesh.num_halfedges(), failure)
        || !resolve_element(second, geometry->working->mesh.num_halfedges(), failure)) return failure;
    auto& mesh = geometry->working->mesh;
    const auto a = WorkingSurfaceMesh::Halfedge_index(first.index);
    const auto b = WorkingSurfaceMesh::Halfedge_index(second.index);
    if (a == b || mesh.face(a) == WorkingSurfaceMesh::null_face()
        || mesh.face(a) != mesh.face(b) || mesh.next(a) == b || mesh.next(b) == a)
        return error(SessionErrorCode::invalid_geometry_output,
            "Facet split requires two non-adjacent halfedges on the same face.");
    const auto created = CGAL::Euler::split_face(a, b, mesh);
    assign_missing_ids(*geometry);
    ++geometry->generation;
    return HalfedgeHandle{session_id_, first.geometry, geometry->generation,
        static_cast<GeometryIndex>(created.idx())};
}

SessionResult<HalfedgeHandle> GeometryEditSession::join_facet(HalfedgeHandle handle)
{
    auto* geometry = resolve({handle.session, handle.geometry});
    if (!geometry) return error(SessionErrorCode::invalid_geometry, "Invalid geometry handle.");
    SessionError failure;
    if (!resolve_element(handle, geometry->working->mesh.num_halfedges(), failure)) return failure;
    auto& mesh = geometry->working->mesh;
    const auto halfedge = WorkingSurfaceMesh::Halfedge_index(handle.index);
    const auto opposite = mesh.opposite(halfedge);
    if (mesh.face(halfedge) == WorkingSurfaceMesh::null_face()
        || mesh.face(opposite) == WorkingSurfaceMesh::null_face()
        || mesh.face(halfedge) == mesh.face(opposite))
        return error(SessionErrorCode::invalid_geometry_output,
            "Facet join requires an edge separating two faces.");
    if (mesh.degree(mesh.source(halfedge)) < 3 || mesh.degree(mesh.target(halfedge)) < 3)
        return error(SessionErrorCode::invalid_geometry_output,
            "Facet join would violate CGAL vertex-degree preconditions.");
    const auto retained_label = geometry->working->face_labels[mesh.face(halfedge)];
    const auto created = CGAL::Euler::join_face(halfedge, mesh);
    const auto retained_face = mesh.face(created);
    if (retained_face != WorkingSurfaceMesh::null_face())
        geometry->working->face_labels[retained_face] = retained_label;
    ++geometry->generation;
    return HalfedgeHandle{session_id_, handle.geometry, geometry->generation,
        static_cast<GeometryIndex>(created.idx())};
}

SessionResult<HalfedgeHandle> GeometryEditSession::flip_edge(HalfedgeHandle handle)
{
    auto* geometry=resolve({handle.session,handle.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    SessionError failure;if(!resolve_element(handle,geometry->working->mesh.num_halfedges(),failure))return failure;
    auto& mesh=geometry->working->mesh;const auto h=WorkingSurfaceMesh::Halfedge_index(handle.index);
    const auto o=mesh.opposite(h);
    if(mesh.is_border(h)||mesh.is_border(o)
        || mesh.next(mesh.next(mesh.next(h)))!=h
        || mesh.next(mesh.next(mesh.next(o)))!=o)
        return error(SessionErrorCode::invalid_geometry_output,
            "Edge flip requires an interior edge separating two triangles.");
    CGAL::Euler::flip_edge(h,mesh);
    ++geometry->generation;
    return HalfedgeHandle{session_id_,handle.geometry,geometry->generation,handle.index};
}

SessionResult<HalfedgeHandle> GeometryEditSession::make_hole(HalfedgeHandle handle)
{
    auto* geometry=resolve({handle.session,handle.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    SessionError failure;if(!resolve_element(handle,geometry->working->mesh.num_halfedges(),failure))return failure;
    auto& mesh=geometry->working->mesh;const auto h=WorkingSurfaceMesh::Halfedge_index(handle.index);
    if(mesh.is_border(h))return error(SessionErrorCode::invalid_geometry_output,"Cannot remove a border face.");
    auto current=h;
    do {if(mesh.is_border(mesh.opposite(current)))return error(SessionErrorCode::invalid_geometry_output,
        "Hole creation requires a face with no incident border edge.");current=mesh.next(current);}while(current!=h);
    CGAL::Euler::make_hole(h,mesh);
    ++geometry->generation;
    return HalfedgeHandle{session_id_,handle.geometry,geometry->generation,handle.index};
}

SessionResult<HalfedgeHandle> GeometryEditSession::fill_hole(HalfedgeHandle handle)
{
    auto* geometry=resolve({handle.session,handle.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    SessionError failure;if(!resolve_element(handle,geometry->working->mesh.num_halfedges(),failure))return failure;
    auto& mesh=geometry->working->mesh;const auto h=WorkingSurfaceMesh::Halfedge_index(handle.index);
    if(!mesh.is_border(h))return error(SessionErrorCode::invalid_geometry_output,"Hole fill requires a border halfedge.");
    CGAL::Euler::fill_hole(h,mesh);
    assign_missing_ids(*geometry);
    ++geometry->generation;
    return HalfedgeHandle{session_id_,handle.geometry,geometry->generation,handle.index};
}

SessionResult<HalfedgeHandle> GeometryEditSession::split_vertex(
    HalfedgeHandle first,HalfedgeHandle second)
{
    if(first.session!=second.session||first.geometry!=second.geometry||first.generation!=second.generation)
        return error(SessionErrorCode::stale_handle,"Vertex split handles do not share one generation.");
    auto* geometry=resolve({first.session,first.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    SessionError failure;if(!resolve_element(first,geometry->working->mesh.num_halfedges(),failure)
        ||!resolve_element(second,geometry->working->mesh.num_halfedges(),failure))return failure;
    auto& mesh=geometry->working->mesh;const auto a=WorkingSurfaceMesh::Halfedge_index(first.index);
    const auto b=WorkingSurfaceMesh::Halfedge_index(second.index);
    if(a==b||mesh.target(a)!=mesh.target(b))return error(SessionErrorCode::invalid_geometry_output,
        "Vertex split requires two distinct halfedges incident to the same target vertex.");
    const auto created=CGAL::Euler::split_vertex(a,b,mesh);
    assign_missing_ids(*geometry);++geometry->generation;
    return HalfedgeHandle{session_id_,first.geometry,geometry->generation,static_cast<GeometryIndex>(created.idx())};
}

SessionResult<HalfedgeHandle> GeometryEditSession::join_vertex(HalfedgeHandle handle)
{
    auto* geometry=resolve({handle.session,handle.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    SessionError failure;if(!resolve_element(handle,geometry->working->mesh.num_halfedges(),failure))return failure;
    auto& mesh=geometry->working->mesh;const auto h=WorkingSurfaceMesh::Halfedge_index(handle.index);
    const auto cycle_degree=[&](auto start){std::size_t n=0;auto current=start;do{++n;current=mesh.next(current);}while(current!=start);return n;};
    if(cycle_degree(h)<4||cycle_degree(mesh.opposite(h))<4)
        return error(SessionErrorCode::invalid_geometry_output,
            "Vertex join requires both incident boundary cycles to have degree at least four.");
    const auto created=CGAL::Euler::join_vertex(h,mesh);
    ++geometry->generation;
    return HalfedgeHandle{session_id_,handle.geometry,geometry->generation,static_cast<GeometryIndex>(created.idx())};
}

SessionResult<HalfedgeHandle> GeometryEditSession::create_center_vertex(HalfedgeHandle handle)
{
    auto* geometry=resolve({handle.session,handle.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    SessionError failure;if(!resolve_element(handle,geometry->working->mesh.num_halfedges(),failure))return failure;
    auto& mesh=geometry->working->mesh;const auto h=WorkingSurfaceMesh::Halfedge_index(handle.index);
    if(mesh.is_border(h))return error(SessionErrorCode::invalid_geometry_output,
        "Center vertex creation requires an incident face.");
    const auto created=CGAL::Euler::add_center_vertex(h,mesh);
    assign_missing_ids(*geometry);++geometry->generation;
    return HalfedgeHandle{session_id_,handle.geometry,geometry->generation,static_cast<GeometryIndex>(created.idx())};
}

SessionResult<HalfedgeHandle> GeometryEditSession::erase_center_vertex(HalfedgeHandle handle)
{
    auto* geometry=resolve({handle.session,handle.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    SessionError failure;if(!resolve_element(handle,geometry->working->mesh.num_halfedges(),failure))return failure;
    auto& mesh=geometry->working->mesh;const auto h=WorkingSurfaceMesh::Halfedge_index(handle.index);
    const auto center=mesh.target(h);
    if(mesh.degree(center)<3)return error(SessionErrorCode::invalid_geometry_output,
        "Center vertex removal requires at least three incident faces.");
    auto current=h;
    do {if(mesh.is_border(current))return error(SessionErrorCode::invalid_geometry_output,
        "Center vertex removal does not accept an incident hole.");current=mesh.opposite(mesh.next(current));}while(current!=h);
    const auto created=CGAL::Euler::remove_center_vertex(h,mesh);
    ++geometry->generation;
    return HalfedgeHandle{session_id_,handle.geometry,geometry->generation,static_cast<GeometryIndex>(created.idx())};
}

std::optional<SessionError> GeometryEditSession::erase_facet(HalfedgeHandle handle)
{
    auto* geometry=resolve({handle.session,handle.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    SessionError failure;if(!resolve_element(handle,geometry->working->mesh.num_halfedges(),failure))return failure;
    auto& mesh=geometry->working->mesh;const auto h=WorkingSurfaceMesh::Halfedge_index(handle.index);
    if(mesh.is_border(h))return error(SessionErrorCode::invalid_geometry_output,"Facet erasure requires an incident face.");
    CGAL::Euler::remove_face(h,mesh);++geometry->generation;return {};
}

std::optional<SessionError> GeometryEditSession::erase_connected_component(HalfedgeHandle handle)
{
    auto* geometry=resolve({handle.session,handle.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    SessionError failure;if(!resolve_element(handle,geometry->working->mesh.num_halfedges(),failure))return failure;
    auto& mesh=geometry->working->mesh;auto face=mesh.face(WorkingSurfaceMesh::Halfedge_index(handle.index));
    if(face==WorkingSurfaceMesh::null_face())face=mesh.face(mesh.opposite(WorkingSurfaceMesh::Halfedge_index(handle.index)));
    if(face==WorkingSurfaceMesh::null_face())return error(SessionErrorCode::invalid_geometry_output,
        "Connected-component erasure requires an incident face.");
    CGAL::Polygon_mesh_processing::remove_connected_components(mesh,std::vector{face});
    ++geometry->generation;return {};
}

SessionResult<std::size_t> GeometryEditSession::keep_largest_connected_components(
    GeometryHandle handle,std::size_t count)
{
    auto* geometry=resolve(handle);if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    const auto removed=CGAL::Polygon_mesh_processing::keep_largest_connected_components(
        geometry->working->mesh,count);
    if(removed>0)++geometry->generation;
    return removed;
}

std::optional<SessionError> GeometryEditSession::clear(GeometryHandle handle)
{
    auto* geometry=resolve(handle);if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    geometry->working->mesh.clear();++geometry->generation;return {};
}

std::optional<SessionError> GeometryEditSession::inside_out(GeometryHandle handle)
{
    auto* geometry=resolve(handle);if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    CGAL::Polygon_mesh_processing::reverse_face_orientations(geometry->working->mesh);
    ++geometry->generation;return {};
}

std::optional<SessionError> GeometryEditSession::normalize_border(GeometryHandle handle)
{
    // Polyhedron_3 used this to partition border halfedges at the end of its
    // physical iterator range. Surface_mesh descriptors are opaque and border
    // discovery is explicit, so there is no observable topology to mutate.
    if(!resolve(handle))return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    return {};
}

SessionResult<HalfedgeHandle> GeometryEditSession::add_vertex_and_facet_to_border(
    HalfedgeHandle first,HalfedgeHandle second)
{
    if(first.session!=second.session||first.geometry!=second.geometry||first.generation!=second.generation)
        return error(SessionErrorCode::stale_handle,"Border handles do not share one generation.");
    auto* geometry=resolve({first.session,first.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    SessionError failure;if(!resolve_element(first,geometry->working->mesh.num_halfedges(),failure)
        ||!resolve_element(second,geometry->working->mesh.num_halfedges(),failure))return failure;
    auto& mesh=geometry->working->mesh;const auto a=WorkingSurfaceMesh::Halfedge_index(first.index);
    const auto b=WorkingSurfaceMesh::Halfedge_index(second.index);
    if(a==b||!mesh.is_border(a)||!mesh.is_border(b))return error(SessionErrorCode::invalid_geometry_output,
        "Border growth requires two distinct border halfedges.");
    auto current=a;bool reachable=false;do{if(current==b){reachable=true;break;}current=mesh.next(current);}while(current!=a);
    if(!reachable)return error(SessionErrorCode::invalid_geometry_output,"Border halfedges are not on the same hole.");
    const auto created=CGAL::Euler::add_vertex_and_face_to_border(a,b,mesh);
    assign_missing_ids(*geometry);++geometry->generation;
    return HalfedgeHandle{session_id_,first.geometry,geometry->generation,static_cast<GeometryIndex>(created.idx())};
}

SessionResult<HalfedgeHandle> GeometryEditSession::add_facet_to_border(
    HalfedgeHandle first,HalfedgeHandle second)
{
    if(first.session!=second.session||first.geometry!=second.geometry||first.generation!=second.generation)
        return error(SessionErrorCode::stale_handle,"Border handles do not share one generation.");
    auto* geometry=resolve({first.session,first.geometry});
    if(!geometry)return error(SessionErrorCode::invalid_geometry,"Invalid geometry handle.");
    SessionError failure;if(!resolve_element(first,geometry->working->mesh.num_halfedges(),failure)
        ||!resolve_element(second,geometry->working->mesh.num_halfedges(),failure))return failure;
    auto& mesh=geometry->working->mesh;const auto a=WorkingSurfaceMesh::Halfedge_index(first.index);
    const auto b=WorkingSurfaceMesh::Halfedge_index(second.index);
    if(a==b||!mesh.is_border(a)||!mesh.is_border(b)||mesh.next(a)==b)
        return error(SessionErrorCode::invalid_geometry_output,
            "Border facet creation requires distinct non-adjacent halfedges on one hole.");
    auto current=a;bool reachable=false;do{if(current==b){reachable=true;break;}current=mesh.next(current);}while(current!=a);
    if(!reachable)return error(SessionErrorCode::invalid_geometry_output,"Border halfedges are not on the same hole.");
    const auto created=CGAL::Euler::add_face_to_border(a,b,mesh);
    assign_missing_ids(*geometry);++geometry->generation;
    return HalfedgeHandle{session_id_,first.geometry,geometry->generation,static_cast<GeometryIndex>(created.idx())};
}

CommitResult GeometryEditSession::commit(const std::vector<GeometryHandle>& outputs)
{
    CommitResult result;
    if (closed_) { result.errors.push_back(error(SessionErrorCode::session_closed, "Geometry edit session is closed.")); return result; }
    for (const auto handle : outputs) {
        if (handle.session != session_id_ || handle.geometry >= geometries_.size()) {
            result.errors.push_back(error(SessionErrorCode::invalid_geometry, "Invalid output geometry handle.")); break;
        }
        auto* geometry = &geometries_[static_cast<std::size_t>(handle.geometry)];
        // An untouched clone can reuse its immutable canonical source directly.
        if (!geometry->working && geometry->source) {
            result.outputs.push_back(geometry->source);
            continue;
        }
        if (!geometry->working) {
            result.errors.push_back(error(SessionErrorCode::invalid_geometry,
                "Output geometry has neither a source nor a working mesh.")); break;
        }
        auto demoted = SurfaceMeshAdapter{}.demote(*geometry->working);
        if (!demoted.success()) { result.errors.push_back(error(SessionErrorCode::invalid_geometry_output,
            demoted.diagnostics.empty() ? "Script output geometry failed CGAL/canonical validation."
                : demoted.diagnostics.front().message)); break; }
        result.outputs.push_back(std::move(demoted.geometry));
    }
    if (!result.errors.empty()) result.outputs.clear();
    closed_ = true; geometries_.clear(); return result;
}

void GeometryEditSession::rollback() noexcept { geometries_.clear(); closed_ = true; }

std::string to_string(SessionErrorCode code)
{
    switch(code) {
    case SessionErrorCode::invalid_session:return "invalid_session"; case SessionErrorCode::invalid_geometry:return "invalid_geometry";
    case SessionErrorCode::stale_handle:return "stale_handle"; case SessionErrorCode::wrong_element:return "wrong_element";
    case SessionErrorCode::invalid_label:return "invalid_label"; case SessionErrorCode::invalid_geometry_output:return "invalid_geometry_output";
    case SessionErrorCode::session_closed:return "session_closed"; } return "unknown";
}

} // namespace phoenix::scripting
