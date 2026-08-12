#include "phoenix/working_geometry.hpp"

#include <CGAL/boost/graph/helpers.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <utility>

namespace phoenix {
namespace {

void diagnostic(std::vector<AdapterDiagnostic>& diagnostics, AdapterDiagnosticCode code,
    GeometryIndex source, std::string message)
{
    diagnostics.push_back(AdapterDiagnostic{code, std::move(message), source});
}

WorkingGeometry make_working_geometry_impl()
{
    WorkingGeometry working;
    working.vertex_ids = working.mesh.add_property_map<WorkingSurfaceMesh::Vertex_index, std::uint64_t>(
        "v:phoenix_id", UINT64_MAX).first;
    working.source_vertices = working.mesh.add_property_map<WorkingSurfaceMesh::Vertex_index, GeometryIndex>(
        "v:source_index", invalid_geometry_index).first;
    working.halfedge_ids = working.mesh.add_property_map<WorkingSurfaceMesh::Halfedge_index, std::uint64_t>(
        "h:phoenix_id", UINT64_MAX).first;
    working.edge_ids = working.mesh.add_property_map<WorkingSurfaceMesh::Halfedge_index, std::uint64_t>(
        "h:edge_id", UINT64_MAX).first;
    working.halfedge_labels = working.mesh.add_property_map<WorkingSurfaceMesh::Halfedge_index, std::int32_t>(
        "h:label", -1).first;
    working.source_halfedges = working.mesh.add_property_map<WorkingSurfaceMesh::Halfedge_index, GeometryIndex>(
        "h:source_index", invalid_geometry_index).first;
    working.face_ids = working.mesh.add_property_map<WorkingSurfaceMesh::Face_index, std::uint64_t>(
        "f:phoenix_id", UINT64_MAX).first;
    working.face_labels = working.mesh.add_property_map<WorkingSurfaceMesh::Face_index, std::int32_t>(
        "f:label", -1).first;
    working.face_tags = working.mesh.add_property_map<WorkingSurfaceMesh::Face_index, std::int32_t>(
        "f:tag", 0).first;
    working.source_faces = working.mesh.add_property_map<WorkingSurfaceMesh::Face_index, GeometryIndex>(
        "f:source_index", invalid_geometry_index).first;
    return working;
}

double squared_distance(const Point3d& left, const Point3d& right)
{
    const auto dx = left.x - right.x;
    const auto dy = left.y - right.y;
    const auto dz = left.z - right.z;
    return dx * dx + dy * dy + dz * dz;
}

double polygon_area_vector_squared(
    const std::vector<GeometryIndex>& loop,
    const std::vector<RuntimeVertex>& vertices)
{
    Point3d area;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const auto& current = vertices[loop[i]].point;
        const auto& next = vertices[loop[(i + 1) % loop.size()]].point;
        area.x += (current.y - next.y) * (current.z + next.z);
        area.y += (current.z - next.z) * (current.x + next.x);
        area.z += (current.x - next.x) * (current.y + next.y);
    }
    return area.x * area.x + area.y * area.y + area.z * area.z;
}

} // namespace

WorkingGeometry create_working_geometry()
{
    return make_working_geometry_impl();
}

PromotionResult SurfaceMeshAdapter::promote(const CanonicalGeometry& source) const
{
    PromotionResult result;
    result.working = create_working_geometry();
    const auto validation = source.validate();
    if (!validation.ok()) {
        for (const auto& issue : validation.issues) {
            diagnostic(result.diagnostics,
                issue.code == GeometryValidationCode::hole_not_supported
                    ? AdapterDiagnosticCode::unsupported_hole
                    : AdapterDiagnosticCode::invalid_runtime_geometry,
                issue.element, issue.message);
        }
        return result;
    }

    const auto& vertices = source.vertices();
    const auto& halfedges = source.halfedges();
    const auto& faces = source.faces();
    for (GeometryIndex i = 0; i < halfedges.size(); ++i) {
        if (halfedges[i].radial_next != invalid_geometry_index) {
            diagnostic(result.diagnostics,
                AdapterDiagnosticCode::unsupported_non_manifold_topology,
                i,
                "This working adapter requires manifold edges; radial incidence is unsupported.");
            return result;
        }
    }
    std::vector<WorkingSurfaceMesh::Vertex_index> vertex_map;
    vertex_map.reserve(vertices.size());
    for (GeometryIndex i = 0; i < vertices.size(); ++i) {
        const auto& vertex = vertices[i];
        const auto target = result.working.mesh.add_vertex(
            WorkingPoint{vertex.point.x, vertex.point.y, vertex.point.z});
        vertex_map.push_back(target);
        result.working.vertex_ids[target] = vertex.id.value();
        result.working.source_vertices[target] = i;
    }

    for (GeometryIndex face_index = 0; face_index < faces.size(); ++face_index) {
        std::vector<WorkingSurfaceMesh::Vertex_index> loop;
        std::vector<GeometryIndex> source_loop;
        auto current = faces[face_index].halfedge;
        do {
            loop.push_back(vertex_map[halfedges[current].origin_vertex]);
            source_loop.push_back(current);
            current = halfedges[current].next;
        } while (current != faces[face_index].halfedge);

        const auto target_face = result.working.mesh.add_face(loop);
        if (target_face == WorkingSurfaceMesh::null_face()) {
            diagnostic(result.diagnostics,
                AdapterDiagnosticCode::unsupported_non_manifold_topology,
                face_index,
                "CGAL Surface_mesh rejected the face; topology is non-manifold or orientation conflicts.");
            return result;
        }
        result.working.face_ids[target_face] = faces[face_index].id.value();
        result.working.face_labels[target_face] = faces[face_index].label.value();
        result.working.source_faces[target_face] = face_index;

        auto target_halfedge = result.working.mesh.halfedge(target_face);
        for (std::size_t i = 0; i < source_loop.size(); ++i) {
            const auto target_origin = result.working.source_vertices[
                result.working.mesh.source(target_halfedge)];
            const auto source_it = std::find_if(
                source_loop.begin(), source_loop.end(),
                [&](GeometryIndex candidate) {
                    return halfedges[candidate].origin_vertex == target_origin;
                });
            if (source_it == source_loop.end()) {
                diagnostic(result.diagnostics,
                    AdapterDiagnosticCode::malformed_working_geometry,
                    face_index,
                    "Could not map a CGAL directed halfedge back to its source edge.");
                return result;
            }
            const auto source_halfedge = *source_it;
            result.working.halfedge_ids[target_halfedge] = halfedges[source_halfedge].id.value();
            result.working.edge_ids[target_halfedge] = halfedges[source_halfedge].edge_id.value();
            result.working.halfedge_labels[target_halfedge] = halfedges[source_halfedge].label.value();
            result.working.source_halfedges[target_halfedge] = source_halfedge;
            target_halfedge = result.working.mesh.next(target_halfedge);
        }
    }
    result.success = true;
    return result;
}

ExtrusionPreparationResult ExtrusionInputAdapter::prepare_face(
    const CanonicalGeometry& source,
    GeometryIndex face_index) const
{
    ExtrusionPreparationResult result;
    const auto validation = source.validate();
    if (!validation.ok()) {
        for (const auto& issue : validation.issues) {
            diagnostic(result.diagnostics, AdapterDiagnosticCode::invalid_runtime_geometry,
                issue.element, issue.message);
        }
        return result;
    }
    if (face_index >= source.faces().size()) {
        diagnostic(result.diagnostics, AdapterDiagnosticCode::invalid_runtime_geometry,
            face_index, "Extrusion source face is out of range.");
        return result;
    }

    const auto& source_face = source.faces()[face_index];
    result.face.source_face_id = source_face.id;
    result.face.face_label = source_face.label;
    auto current = source_face.halfedge;
    do {
        const auto& halfedge = source.halfedges()[current];
        result.face.boundary.push_back(ExtrusionWorkingPoint{
            source.vertices()[halfedge.origin_vertex].point,
            source.vertices()[halfedge.origin_vertex].id,
            halfedge.id,
            halfedge.edge_id,
            halfedge.label});
        current = halfedge.next;
    } while (current != source_face.halfedge);
    result.success = true;
    return result;
}

DemotionResult SurfaceMeshAdapter::demote(const WorkingGeometry& source) const
{
    DemotionResult result;
    std::vector<RuntimeVertex> vertices;
    vertices.reserve(source.mesh.number_of_vertices());
    std::map<WorkingSurfaceMesh::Vertex_index, GeometryIndex> vertex_map;
    for (const auto vertex : source.mesh.vertices()) {
        const auto& point = source.mesh.point(vertex);
        const auto index = static_cast<GeometryIndex>(vertices.size());
        vertex_map.emplace(vertex, index);
        vertices.push_back(RuntimeVertex{
            {CGAL::to_double(point.x()), CGAL::to_double(point.y()), CGAL::to_double(point.z())},
            VertexId{source.vertex_ids[vertex]}, invalid_geometry_index});
    }

    std::vector<RuntimeHalfedge> halfedges;
    std::vector<RuntimeFace> faces;
    std::map<WorkingSurfaceMesh::Halfedge_index, GeometryIndex> halfedge_map;
    for (const auto face : source.mesh.faces()) {
        const auto face_index = static_cast<GeometryIndex>(faces.size());
        const auto first = source.mesh.halfedge(face);
        auto current = first;
        const auto first_runtime = static_cast<GeometryIndex>(halfedges.size());
        do {
            const auto runtime_index = static_cast<GeometryIndex>(halfedges.size());
            halfedge_map.emplace(current, runtime_index);
            halfedges.push_back(RuntimeHalfedge{
                vertex_map.at(source.mesh.source(current)), face_index,
                invalid_geometry_index, invalid_geometry_index,
                invalid_geometry_index, invalid_geometry_index,
                HalfedgeId{source.halfedge_ids[current]}, EdgeId{source.edge_ids[current]},
                LabelId{source.halfedge_labels[current]}});
            if (vertices[halfedges.back().origin_vertex].outgoing_halfedge == invalid_geometry_index) {
                vertices[halfedges.back().origin_vertex].outgoing_halfedge = runtime_index;
            }
            current = source.mesh.next(current);
        } while (current != first);
        const auto end = static_cast<GeometryIndex>(halfedges.size());
        for (GeometryIndex i = first_runtime; i < end; ++i) {
            halfedges[i].next = i + 1 == end ? first_runtime : i + 1;
            halfedges[i].previous = i == first_runtime ? end - 1 : i - 1;
        }
        faces.push_back(RuntimeFace{
            first_runtime, FaceId{source.face_ids[face]}, LabelId{source.face_labels[face]}});
    }

    for (const auto& entry : halfedge_map) {
        const auto opposite = source.mesh.opposite(entry.first);
        const auto found = halfedge_map.find(opposite);
        if (found != halfedge_map.end()) halfedges[entry.second].opposite = found->second;
    }

    GeometryValidationResult validation;
    result.geometry = CanonicalGeometry::create(
        std::move(vertices), std::move(halfedges), std::move(faces), &validation);
    if (!result.geometry) {
        for (const auto& issue : validation.issues) {
            diagnostic(result.diagnostics, AdapterDiagnosticCode::malformed_working_geometry,
                issue.element, issue.message);
        }
    }
    return result;
}

GeometryRepairer::GeometryRepairer(RepairPolicy policy) : policy_(std::move(policy))
{
    if (!(policy_.absolute_tolerance > 0.0) || !std::isfinite(policy_.absolute_tolerance)) {
        policy_.absolute_tolerance = 1e-6;
    }
}

DemotionResult GeometryRepairer::repair(const CanonicalGeometry& source) const
{
    DemotionResult result;
    const auto tolerance2 = policy_.absolute_tolerance * policy_.absolute_tolerance;
    std::vector<RuntimeVertex> vertices;
    std::vector<GeometryIndex> remap(source.vertices().size(), invalid_geometry_index);
    std::vector<bool> merged_targets;
    for (GeometryIndex i = 0; i < source.vertices().size(); ++i) {
        GeometryIndex target = invalid_geometry_index;
        for (GeometryIndex j = 0; j < vertices.size(); ++j) {
            if (squared_distance(source.vertices()[i].point, vertices[j].point) <= tolerance2) {
                target = j;
                break;
            }
        }
        if (target == invalid_geometry_index) {
            target = static_cast<GeometryIndex>(vertices.size());
            vertices.push_back(source.vertices()[i]);
            vertices.back().outgoing_halfedge = invalid_geometry_index;
            merged_targets.push_back(false);
        } else {
            vertices[target].id = VertexId{};
            merged_targets[target] = true;
            diagnostic(result.diagnostics, AdapterDiagnosticCode::merged_near_vertices, i,
                "Merged vertices within the absolute repair tolerance.");
        }
        remap[i] = target;
    }

    std::vector<RuntimeHalfedge> halfedges;
    std::vector<RuntimeFace> faces;
    for (GeometryIndex face_index = 0; face_index < source.faces().size(); ++face_index) {
        std::vector<GeometryIndex> loop;
        std::vector<const RuntimeHalfedge*> source_edges;
        bool face_changed = false;
        auto current = source.faces()[face_index].halfedge;
        do {
            const auto mapped = remap[source.halfedges()[current].origin_vertex];
            if (loop.empty() || loop.back() != mapped) {
                loop.push_back(mapped);
                source_edges.push_back(&source.halfedges()[current]);
            } else {
                face_changed = true;
                diagnostic(result.diagnostics, AdapterDiagnosticCode::removed_zero_length_edge,
                    current, "Removed a zero-length edge after vertex repair.");
                if (source.halfedges()[current].label.is_registered()) {
                    diagnostic(result.diagnostics, AdapterDiagnosticCode::removed_labeled_topology,
                        current, "Repair removed a labeled degenerate edge.");
                }
            }
            current = source.halfedges()[current].next;
        } while (current != source.faces()[face_index].halfedge);
        if (loop.size() > 1 && loop.front() == loop.back()) {
            face_changed = true;
            loop.pop_back();
            source_edges.pop_back();
        }
        if (loop.size() < 3 || polygon_area_vector_squared(loop, vertices) <= tolerance2 * tolerance2) {
            diagnostic(result.diagnostics, AdapterDiagnosticCode::removed_zero_area_face,
                face_index, "Removed a zero-area face.");
            if (source.faces()[face_index].label.is_registered()) {
                diagnostic(result.diagnostics, AdapterDiagnosticCode::removed_labeled_topology,
                    face_index, "Repair removed a labeled degenerate face.");
            }
            continue;
        }

        const auto new_face = static_cast<GeometryIndex>(faces.size());
        const auto first_halfedge = static_cast<GeometryIndex>(halfedges.size());
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const auto runtime_index = static_cast<GeometryIndex>(halfedges.size());
            const auto* source_edge = source_edges[i];
            const auto endpoint_changed = merged_targets[loop[i]]
                || merged_targets[loop[(i + 1) % loop.size()]];
            halfedges.push_back(RuntimeHalfedge{
                loop[i], new_face,
                first_halfedge + static_cast<GeometryIndex>((i + 1) % loop.size()),
                first_halfedge + static_cast<GeometryIndex>((i + loop.size() - 1) % loop.size()),
                invalid_geometry_index, invalid_geometry_index,
                endpoint_changed ? HalfedgeId{} : source_edge->id,
                endpoint_changed ? EdgeId{} : source_edge->edge_id,
                source_edge->label});
            if (vertices[loop[i]].outgoing_halfedge == invalid_geometry_index) {
                vertices[loop[i]].outgoing_halfedge = runtime_index;
            }
        }
        faces.push_back(RuntimeFace{
            first_halfedge,
            (face_changed || std::any_of(loop.begin(), loop.end(),
                [&](GeometryIndex vertex) { return merged_targets[vertex]; }))
                ? FaceId{} : source.faces()[face_index].id,
            source.faces()[face_index].label});
    }

    // Restore unique manifold opposites by endpoint pair. Radial non-manifold
    // reconstruction remains outside this manifold repair policy.
    std::map<std::pair<GeometryIndex, GeometryIndex>, GeometryIndex> directed;
    for (GeometryIndex i = 0; i < halfedges.size(); ++i) {
        const auto destination = halfedges[halfedges[i].next].origin_vertex;
        const auto reverse = directed.find({destination, halfedges[i].origin_vertex});
        if (reverse != directed.end()) {
            halfedges[i].opposite = reverse->second;
            halfedges[reverse->second].opposite = i;
        } else {
            directed.emplace(std::make_pair(halfedges[i].origin_vertex, destination), i);
        }
    }

    GeometryValidationResult validation;
    result.geometry = CanonicalGeometry::create(
        std::move(vertices), std::move(halfedges), std::move(faces), &validation);
    if (!result.geometry) {
        for (const auto& issue : validation.issues) {
            diagnostic(result.diagnostics, AdapterDiagnosticCode::malformed_working_geometry,
                issue.element, issue.message);
        }
    }
    return result;
}

std::string to_string(AdapterDiagnosticCode code)
{
    switch (code) {
    case AdapterDiagnosticCode::invalid_runtime_geometry: return "invalid_runtime_geometry";
    case AdapterDiagnosticCode::unsupported_non_manifold_topology: return "unsupported_non_manifold_topology";
    case AdapterDiagnosticCode::unsupported_hole: return "unsupported_hole";
    case AdapterDiagnosticCode::orientation_conflict: return "orientation_conflict";
    case AdapterDiagnosticCode::cgal_face_insertion_failed: return "cgal_face_insertion_failed";
    case AdapterDiagnosticCode::malformed_working_geometry: return "malformed_working_geometry";
    case AdapterDiagnosticCode::removed_zero_length_edge: return "removed_zero_length_edge";
    case AdapterDiagnosticCode::removed_zero_area_face: return "removed_zero_area_face";
    case AdapterDiagnosticCode::merged_near_vertices: return "merged_near_vertices";
    case AdapterDiagnosticCode::removed_labeled_topology: return "removed_labeled_topology";
    }
    return "unknown";
}

} // namespace phoenix
