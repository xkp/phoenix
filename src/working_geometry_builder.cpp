#include "phoenix/working_geometry_builder.hpp"

#include <string>

namespace phoenix {
namespace {
void add_diagnostic(WorkingGeometryBuildResult& result, AdapterDiagnosticCode code,
    GeometryIndex element, std::string message)
{
    result.diagnostics.push_back({code, std::move(message), element});
}
} // namespace

WorkingGeometryBuilder::WorkingGeometryBuilder(RunElementIdAllocator& ids) noexcept : ids_(&ids) {}

WorkingVertexIndex WorkingGeometryBuilder::add_vertex(Point3d point)
{
    vertices_.push_back({point, ids_->next_vertex()});
    return vertices_.size() - 1;
}

void WorkingGeometryBuilder::set_vertex_id(WorkingVertexIndex vertex, VertexId id)
{
    if (vertex < vertices_.size()) vertices_[vertex].id = id;
}

WorkingFaceIndex WorkingGeometryBuilder::begin_facet()
{
    if (open_face_ != invalid_working_face_index) return invalid_working_face_index;
    faces_.push_back({});
    open_face_ = faces_.size() - 1;
    faces_.back().id = ids_->next_face();
    return open_face_;
}

bool WorkingGeometryBuilder::add_vertex_to_facet(WorkingVertexIndex vertex)
{
    if (open_face_ == invalid_working_face_index || vertex >= vertices_.size()) return false;
    faces_[open_face_].vertices.push_back(vertex);
    return true;
}

bool WorkingGeometryBuilder::end_facet()
{
    if (open_face_ == invalid_working_face_index || faces_[open_face_].vertices.size() < 3)
        return false;
    faces_[open_face_].closed = true;
    open_face_ = invalid_working_face_index;
    return true;
}

void WorkingGeometryBuilder::set_face_label(WorkingFaceIndex face, LabelId label)
{ if (face < faces_.size()) faces_[face].label = label; }

void WorkingGeometryBuilder::set_face_tag(WorkingFaceIndex face, std::int32_t tag)
{ if (face < faces_.size()) faces_[face].tag = tag; }

void WorkingGeometryBuilder::set_halfedge_label_by_target(
    WorkingFaceIndex face, WorkingVertexIndex target, LabelId label)
{
    if (face >= faces_.size()) return;
    for (auto& item : faces_[face].directed) if (item.target == target) { item.label = label; return; }
    faces_[face].directed.push_back({target, label, {}, {}});
}

void WorkingGeometryBuilder::set_halfedge_id_by_target(
    WorkingFaceIndex face, WorkingVertexIndex target, HalfedgeId id)
{
    if (face >= faces_.size()) return;
    for (auto& item : faces_[face].directed) if (item.target == target) { item.id = id; return; }
    faces_[face].directed.push_back({target, unassigned_label_id, id, {}});
}

void WorkingGeometryBuilder::set_edge_id_by_vertices(
    WorkingVertexIndex source, WorkingVertexIndex target, EdgeId id)
{
    for (auto& face : faces_) {
        for (std::size_t i = 0; i < face.vertices.size(); ++i) {
            if (face.vertices[i] != source
                || face.vertices[(i + 1) % face.vertices.size()] != target) continue;
            for (auto& item : face.directed) {
                if (item.target == target) { item.edge_id = id; return; }
            }
            face.directed.push_back({target, unassigned_label_id, {}, id});
            return;
        }
    }
}

void WorkingGeometryBuilder::set_halfedge_id_by_vertices(
    WorkingVertexIndex source, WorkingVertexIndex target, HalfedgeId id)
{
    for (std::size_t face = 0; face < faces_.size(); ++face) {
        const auto& vertices = faces_[face].vertices;
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            if (vertices[i] == source && vertices[(i + 1) % vertices.size()] == target) {
                set_halfedge_id_by_target(face, target, id);
            }
        }
    }
}

WorkingGeometryBuildResult WorkingGeometryBuilder::build() const
{
    WorkingGeometryBuildResult result;
    result.working = create_working_geometry();
    if (open_face_ != invalid_working_face_index) {
        add_diagnostic(result, AdapterDiagnosticCode::malformed_working_geometry,
            static_cast<GeometryIndex>(open_face_), "Cannot build with an open facet.");
        return result;
    }
    std::vector<WorkingSurfaceMesh::Vertex_index> vertex_map;
    for (const auto& source : vertices_) {
        const auto vertex = result.working.mesh.add_vertex(
            {source.point.x, source.point.y, source.point.z});
        vertex_map.push_back(vertex);
        result.working.vertex_ids[vertex] = source.id.value();
    }
    for (std::size_t face_index = 0; face_index < faces_.size(); ++face_index) {
        const auto& source = faces_[face_index];
        if (!source.closed) {
            add_diagnostic(result, AdapterDiagnosticCode::malformed_working_geometry,
                static_cast<GeometryIndex>(face_index), "Facet was not closed.");
            return result;
        }
        std::vector<WorkingSurfaceMesh::Vertex_index> loop;
        for (const auto vertex : source.vertices) loop.push_back(vertex_map[vertex]);
        const auto face = result.working.mesh.add_face(loop);
        if (face == WorkingSurfaceMesh::null_face()) {
            add_diagnostic(result, AdapterDiagnosticCode::cgal_face_insertion_failed,
                static_cast<GeometryIndex>(face_index), "CGAL rejected a working-geometry facet.");
            return result;
        }
        result.working.face_ids[face] = source.id.value();
        result.working.face_labels[face] = source.label.value();
        result.working.face_tags[face] = source.tag;
        auto halfedge = result.working.mesh.halfedge(face);
        do {
            const auto target = static_cast<WorkingVertexIndex>(
                result.working.mesh.target(halfedge).idx());
            const DirectedMetadata* found = nullptr;
            for (const auto& item : source.directed) if (item.target == target) { found = &item; break; }
            result.working.halfedge_ids[halfedge] = found && found->id.valid()
                ? found->id.value() : ids_->next_halfedge().value();
            result.working.halfedge_labels[halfedge] = found
                ? found->label.value() : unassigned_label_id.value();
            if (found && found->edge_id.valid())
                result.working.edge_ids[halfedge] = found->edge_id.value();
            halfedge = result.working.mesh.next(halfedge);
        } while (halfedge != result.working.mesh.halfedge(face));
    }
    for (const auto edge : result.working.mesh.edges()) {
        const auto halfedge = result.working.mesh.halfedge(edge);
        const auto opposite = result.working.mesh.opposite(halfedge);
        const auto forward_id = result.working.edge_ids[halfedge];
        const auto reverse_id = result.working.edge_ids[opposite];
        const auto id = forward_id != UINT64_MAX ? forward_id
            : reverse_id != UINT64_MAX ? reverse_id : ids_->next_edge().value();
        result.working.edge_ids[halfedge] = id;
        result.working.edge_ids[opposite] = id;
    }
    result.success = true;
    return result;
}

} // namespace phoenix
