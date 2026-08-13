#include "phoenix/merge/production_adapter.hpp"

#include "phoenix/working_geometry_builder.hpp"

#include <CGAL/Modifier_base.h>
#include <CGAL/Polyhedron_incremental_builder_3.h>

#include <map>
#include <set>
#include <utility>

namespace phoenix::merge {

GeometryItemEffect ProductionMergeResult::publication_effect(std::uint64_t item_key) const
{
    GeometryItemEffect effect;
    effect.item_key = item_key;
    effect.succeeded = success();
    effect.generated_geometry = geometry;
    effect.failure_message = error;
    if (success()) effect.consumed_faces = consumed_faces;
    return effect;
}

namespace {

struct VertexKey {
    const CanonicalGeometry* geometry = nullptr;
    GeometryIndex index = invalid_geometry_index;
    friend bool operator<(const VertexKey& left, const VertexKey& right)
    {
        return left.geometry < right.geometry
            || (left.geometry == right.geometry && left.index < right.index);
    }
};

struct FaceRecord {
    LabelId label;
    FaceId id;
    std::vector<std::size_t> vertices;
    std::vector<LabelId> labels;
    std::vector<HalfedgeId> halfedge_ids;
};

class ExactMeshBuilder : public CGAL::Modifier_base<geometry::polyhedron3::HalfedgeDS> {
public:
    ExactMeshBuilder(std::vector<Point3d> points, std::vector<VertexId> vertex_ids,
        std::vector<FaceRecord> faces)
        : points_(std::move(points)), vertex_ids_(std::move(vertex_ids)),
          faces_(std::move(faces)) {}

    void operator()(geometry::polyhedron3::HalfedgeDS& hds) override
    {
        CGAL::Polyhedron_incremental_builder_3<geometry::polyhedron3::HalfedgeDS>
            builder(hds, true);
        builder.begin_surface(points_.size(), faces_.size());
        for (std::size_t index = 0; index < points_.size(); ++index) {
            const auto& point = points_[index];
            auto vertex = builder.add_vertex({point.x, point.y, point.z});
            vertex->data.id = static_cast<int>(vertex_ids_[index].value());
        }
        for (const auto& face : faces_) {
            if (!builder.test_facet(face.vertices.begin(), face.vertices.end())) {
                failed_ = true;
                break;
            }
            auto halfedge = builder.add_facet(face.vertices.begin(), face.vertices.end());
            halfedge->face()->data.id = static_cast<int>(face.id.value());
            halfedge->face()->data.label = face.label.value();
            auto current = halfedge;
            do {
                const auto target_id = current->vertex()->data.id;
                for (std::size_t index = 0; index < face.vertices.size(); ++index) {
                    if (static_cast<int>(vertex_ids_[face.vertices[index]].value()) != target_id)
                        continue;
                    current->data.id = static_cast<int>(face.halfedge_ids[index].value());
                    current->data.label = face.labels[index].value();
                    break;
                }
                current = current->next();
            } while (current != halfedge);
        }
        builder.end_surface();
    }

    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    std::vector<Point3d> points_;
    std::vector<VertexId> vertex_ids_;
    std::vector<FaceRecord> faces_;
    bool failed_ = false;
};

bool build_exact_input(const std::vector<SourceFace>& sources,
    geometry::polyhedron3& output, std::string& error)
{
    std::map<VertexKey, std::size_t> vertex_map;
    std::vector<Point3d> points;
    std::vector<VertexId> vertex_ids;
    std::vector<FaceRecord> faces;
    for (const auto& source : sources) {
        if (!source.face.valid()) {
            error = "Merge source contains an invalid face reference.";
            return false;
        }
        const auto& geometry = *source.face.geometry;
        const auto& runtime_face = geometry.faces()[source.face.face_index];
        FaceRecord record;
        record.label = runtime_face.label;
        record.id = runtime_face.id;
        auto halfedge = runtime_face.halfedge;
        do {
            const auto& edge = geometry.halfedges()[halfedge];
            const VertexKey key{&geometry, edge.origin_vertex};
            auto found = vertex_map.find(key);
            if (found == vertex_map.end()) {
                const auto index = points.size();
                points.push_back(geometry.vertices()[edge.origin_vertex].point);
                vertex_ids.push_back(geometry.vertices()[edge.origin_vertex].id);
                found = vertex_map.emplace(key, index).first;
            }
            record.vertices.push_back(found->second);
            record.labels.push_back(edge.label);
            record.halfedge_ids.push_back(edge.id);
            halfedge = edge.next;
        } while (halfedge != runtime_face.halfedge);
        faces.push_back(std::move(record));
    }
    ExactMeshBuilder builder(std::move(points), std::move(vertex_ids), std::move(faces));
    output.delegate(builder);
    if (builder.failed() || !output.is_valid(false, 0)) {
        error = "Merge sources do not form a valid oriented manifold working mesh.";
        return false;
    }
    return true;
}

CanonicalGeometryRef publish(const geometry::polyhedron3& mesh,
    RunElementIdAllocator& ids, std::string& error)
{
    WorkingGeometryBuilder builder(ids);
    std::map<const void*, WorkingVertexIndex> vertices;
    for (auto vertex = mesh.vertices_begin(); vertex != mesh.vertices_end(); ++vertex) {
        const auto& point = vertex->point();
        vertices.emplace(&*vertex, builder.add_vertex({CGAL::to_double(point.x()),
            CGAL::to_double(point.y()), CGAL::to_double(point.z())}));
    }
    for (auto face = mesh.facets_begin(); face != mesh.facets_end(); ++face) {
        const auto output_face = builder.begin_facet();
        std::vector<std::pair<LabelId, WorkingVertexIndex>> loop;
        auto edge = face->facet_begin();
        const auto begin = edge;
        do {
            const auto vertex = vertices.at(&*edge->opposite()->vertex());
            loop.emplace_back(LabelId{edge->data.label}, vertex);
            builder.add_vertex_to_facet(vertex);
            ++edge;
        } while (edge != begin);
        if (!builder.end_facet()) {
            error = "Merge produced an invalid face boundary.";
            return nullptr;
        }
        builder.set_face_label(output_face, LabelId{face->data.label});
        for (std::size_t index = 0; index < loop.size(); ++index) {
            builder.set_halfedge_label_by_target(output_face,
                loop[(index + 1) % loop.size()].second,
                loop[index].first);
        }
    }
    const auto built = builder.build();
    if (!built.success) {
        error = built.diagnostics.empty() ? "Merge output publication failed."
                                          : built.diagnostics.front().message;
        return nullptr;
    }
    const auto demoted = SurfaceMeshAdapter{}.demote(built.working);
    if (!demoted.success()) {
        error = demoted.diagnostics.empty() ? "Merge output demotion failed."
                                            : demoted.diagnostics.front().message;
        return nullptr;
    }
    return demoted.geometry;
}

} // namespace

ProductionMergeResult run_production_merge(
    const ProductionMergeRequest& request, RunElementIdAllocator& ids)
{
    ProductionMergeResult result;
    if (request.sources.empty()) {
        result.error = "Merge requires at least one source face.";
        return result;
    }
    geometry::polyhedron3 input;
    if (!build_exact_input(request.sources, input, result.error)) return result;
    auto vertex_id = [&ids] { return static_cast<long>(ids.next_vertex().value()); };
    auto edge_id = [&ids] { return static_cast<long>(ids.next_halfedge().value()); };
    auto pipeline = run_production_pipeline(input, request.options, vertex_id, edge_id);
    if (!pipeline.success()) {
        result.error = std::move(pipeline.error);
        return result;
    }
    result.geometry = publish(pipeline.mesh, ids, result.error);
    if (!result.geometry) return result;
    for (const auto& source : request.sources)
        result.consumed_faces.push_back({source.owner_actor_id, source.face.face_id});
    return result;
}

} // namespace phoenix::merge
