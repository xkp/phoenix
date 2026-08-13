#include "phoenix/smooth/subdivision.hpp"

#include "phoenix/working_geometry_builder.hpp"

#include <opensubdiv/far/primvarRefiner.h>
#include <opensubdiv/far/topologyDescriptor.h>
#include <opensubdiv/far/topologyRefinerFactory.h>

#include <memory>
#include <vector>

namespace phoenix::smooth {
namespace {

struct Vertex {
    double p[3]{};
    void Clear(void* = nullptr) { p[0] = p[1] = p[2] = 0.0; }
    void AddWithWeight(const Vertex& source, double weight)
    {
        p[0] += source.p[0] * weight;
        p[1] += source.p[1] * weight;
        p[2] += source.p[2] * weight;
    }
};

OpenSubdiv::Sdc::SchemeType scheme_type(SubdivisionScheme scheme)
{
    switch (scheme) {
    case SubdivisionScheme::bilinear: return OpenSubdiv::Sdc::SCHEME_BILINEAR;
    case SubdivisionScheme::catmark: return OpenSubdiv::Sdc::SCHEME_CATMARK;
    case SubdivisionScheme::loop: return OpenSubdiv::Sdc::SCHEME_LOOP;
    }
    return OpenSubdiv::Sdc::SCHEME_BILINEAR;
}

OpenSubdiv::Sdc::Options::VtxBoundaryInterpolation boundary_type(
    BoundaryInterpolation boundary)
{
    using Boundary = OpenSubdiv::Sdc::Options::VtxBoundaryInterpolation;
    switch (boundary) {
    case BoundaryInterpolation::none: return Boundary::VTX_BOUNDARY_NONE;
    case BoundaryInterpolation::edge_only: return Boundary::VTX_BOUNDARY_EDGE_ONLY;
    case BoundaryInterpolation::edge_and_corner: return Boundary::VTX_BOUNDARY_EDGE_AND_CORNER;
    }
    return Boundary::VTX_BOUNDARY_EDGE_ONLY;
}

bool append_face_loop(const CanonicalGeometry& source, GeometryIndex face_index,
    std::vector<int>& counts, std::vector<OpenSubdiv::Far::Index>& indices,
    std::vector<LabelId>& labels)
{
    const auto& face = source.faces()[face_index];
    if (face.halfedge >= source.halfedges().size()) return false;
    const auto begin = face.halfedge;
    auto current = begin;
    int count = 0;
    do {
        if (current >= source.halfedges().size()) return false;
        const auto& halfedge = source.halfedges()[current];
        if (halfedge.origin_vertex >= source.vertices().size() || halfedge.face != face_index)
            return false;
        indices.push_back(static_cast<OpenSubdiv::Far::Index>(halfedge.origin_vertex));
        ++count;
        current = halfedge.next;
        if (count > static_cast<int>(source.halfedges().size())) return false;
    } while (current != begin);
    if (count < 3) return false;
    counts.push_back(count);
    labels.push_back(face.label);
    return true;
}

int coarse_ancestor(const OpenSubdiv::Far::TopologyRefiner& refiner, int level, int face)
{
    while (level > 0) {
        face = refiner.GetLevel(level).GetFaceParentFace(face);
        --level;
    }
    return face;
}

} // namespace

SubdivisionResult subdivide(const CanonicalGeometry& source, RunElementIdAllocator& ids,
    const SubdivisionOptions& options)
{
    SubdivisionResult result;
    if (options.max_refinement_level == 0) {
        result.geometry = CanonicalGeometry::create(
            source.vertices(), source.halfedges(), source.faces());
        return result;
    }
    if (source.faces().empty() || source.vertices().empty()) {
        result.diagnostics.push_back({"Subdivision requires non-empty geometry."});
        return result;
    }

    std::vector<int> face_counts;
    std::vector<OpenSubdiv::Far::Index> face_vertices;
    std::vector<LabelId> coarse_labels;
    face_counts.reserve(source.faces().size());
    coarse_labels.reserve(source.faces().size());
    for (GeometryIndex face = 0; face < source.faces().size(); ++face) {
        if (!append_face_loop(source, face, face_counts, face_vertices, coarse_labels)) {
            result.diagnostics.push_back({"Canonical face loop cannot be adapted to OpenSubdiv."});
            return result;
        }
        if (options.scheme == SubdivisionScheme::loop && face_counts.back() != 3) {
            result.diagnostics.push_back({"Loop subdivision requires triangular input faces."});
            return result;
        }
    }

    OpenSubdiv::Far::TopologyDescriptor descriptor;
    descriptor.numVertices = static_cast<int>(source.vertices().size());
    descriptor.numFaces = static_cast<int>(face_counts.size());
    descriptor.numVertsPerFace = face_counts.data();
    descriptor.vertIndicesPerFace = face_vertices.data();

    OpenSubdiv::Sdc::Options sdc_options;
    sdc_options.SetVtxBoundaryInterpolation(boundary_type(options.boundary_interpolation));
    using Factory = OpenSubdiv::Far::TopologyRefinerFactory<OpenSubdiv::Far::TopologyDescriptor>;
    std::unique_ptr<OpenSubdiv::Far::TopologyRefiner> refiner(Factory::Create(
        descriptor, Factory::Options(scheme_type(options.scheme), sdc_options)));
    if (!refiner) {
        result.diagnostics.push_back({"OpenSubdiv rejected the input topology."});
        return result;
    }

    OpenSubdiv::Far::TopologyRefiner::UniformOptions refine_options(
        static_cast<int>(options.max_refinement_level));
    refine_options.fullTopologyInLastLevel = true;
    refiner->RefineUniform(refine_options);

    std::vector<Vertex> vertices(static_cast<std::size_t>(refiner->GetNumVerticesTotal()));
    for (std::size_t i = 0; i < source.vertices().size(); ++i) {
        const auto& point = source.vertices()[i].point;
        vertices[i].p[0] = point.x;
        vertices[i].p[1] = point.y;
        vertices[i].p[2] = point.z;
    }
    OpenSubdiv::Far::PrimvarRefiner primvars(*refiner);
    Vertex* parent = vertices.data();
    for (int level = 1; level <= static_cast<int>(options.max_refinement_level); ++level) {
        Vertex* child = parent + refiner->GetLevel(level - 1).GetNumVertices();
        primvars.Interpolate(level, parent, child);
        parent = child;
    }

    const int level_number = static_cast<int>(options.max_refinement_level);
    const auto& level = refiner->GetLevel(level_number);
    WorkingGeometryBuilder builder(ids);
    std::vector<WorkingVertexIndex> output_vertices;
    output_vertices.reserve(static_cast<std::size_t>(level.GetNumVertices()));
    for (int vertex = 0; vertex < level.GetNumVertices(); ++vertex) {
        const auto& p = parent[vertex].p;
        output_vertices.push_back(builder.add_vertex({p[0], p[1], p[2]}));
    }
    for (int face = 0; face < level.GetNumFaces(); ++face) {
        const auto output_face = builder.begin_facet();
        for (const auto vertex : level.GetFaceVertices(face)) {
            if (!builder.add_vertex_to_facet(output_vertices[static_cast<std::size_t>(vertex)])) {
                result.diagnostics.push_back({"Failed to stage a refined face vertex."});
                return result;
            }
        }
        if (!builder.end_facet()) {
            result.diagnostics.push_back({"Failed to close a refined face."});
            return result;
        }
        const int ancestor = coarse_ancestor(*refiner, level_number, face);
        if (ancestor < 0 || static_cast<std::size_t>(ancestor) >= coarse_labels.size()) {
            result.diagnostics.push_back({"OpenSubdiv returned an invalid coarse face ancestor."});
            return result;
        }
        builder.set_face_label(output_face, coarse_labels[static_cast<std::size_t>(ancestor)]);
        builder.set_face_tag(output_face, options.smoothing_group);
    }
    auto working = builder.build();
    if (!working.success) {
        result.diagnostics.push_back({"CGAL rejected the refined topology during publication."});
        return result;
    }
    auto demoted = SurfaceMeshAdapter{}.demote(working.working);
    if (!demoted.success()) {
        result.diagnostics.push_back({"Refined topology could not be published canonically."});
        return result;
    }
    result.geometry = std::move(demoted.geometry);
    return result;
}

} // namespace phoenix::smooth
