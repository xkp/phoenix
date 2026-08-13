#include "phoenix/publication.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace phoenix {
namespace {

struct FaceCopy {
    const CanonicalGeometry* geometry = nullptr;
    GeometryIndex face_index = invalid_geometry_index;
};

CanonicalGeometryRef combine_faces(
    const std::vector<FaceCopy>& copies,
    RunElementIdAllocator& allocator)
{
    std::vector<RuntimeVertex> vertices;
    std::vector<RuntimeHalfedge> halfedges;
    std::vector<RuntimeFace> faces;

    std::map<std::pair<const CanonicalGeometry*, GeometryIndex>, GeometryIndex> copied_vertices;
    std::set<std::uint64_t> used_ids;
    auto unique_vertex_id = [&](VertexId candidate) {
        if (candidate.valid() && used_ids.insert(candidate.value()).second) return candidate;
        VertexId generated;
        do generated = allocator.next_vertex(); while (!used_ids.insert(generated.value()).second);
        return generated;
    };
    auto unique_halfedge_id = [&](HalfedgeId candidate) {
        if (candidate.valid() && used_ids.insert(candidate.value()).second) return candidate;
        HalfedgeId generated;
        do generated = allocator.next_halfedge(); while (!used_ids.insert(generated.value()).second);
        return generated;
    };
    auto unique_edge_id = [&](EdgeId candidate) {
        if (candidate.valid() && used_ids.insert(candidate.value()).second) return candidate;
        EdgeId generated;
        do generated = allocator.next_edge(); while (!used_ids.insert(generated.value()).second);
        return generated;
    };
    auto unique_face_id = [&](FaceId candidate) {
        if (candidate.valid() && used_ids.insert(candidate.value()).second) return candidate;
        FaceId generated;
        do generated = allocator.next_face(); while (!used_ids.insert(generated.value()).second);
        return generated;
    };

    for (const auto& copy : copies) {
        const auto& source_face = copy.geometry->faces()[copy.face_index];
        std::vector<GeometryIndex> source_loop;
        auto current = source_face.halfedge;
        do {
            source_loop.push_back(current);
            current = copy.geometry->halfedges()[current].next;
        } while (current != source_face.halfedge);

        const auto target_face_index = static_cast<GeometryIndex>(faces.size());
        const auto first_halfedge = static_cast<GeometryIndex>(halfedges.size());
        for (std::size_t i = 0; i < source_loop.size(); ++i) {
            const auto& source_halfedge = copy.geometry->halfedges()[source_loop[i]];
            const auto source_vertex_index = source_halfedge.origin_vertex;
            const auto vertex_key = std::make_pair(copy.geometry, source_vertex_index);
            auto vertex_it = copied_vertices.find(vertex_key);
            if (vertex_it == copied_vertices.end()) {
                auto vertex = copy.geometry->vertices()[source_vertex_index];
                vertex.id = unique_vertex_id(vertex.id);
                vertex.outgoing_halfedge = first_halfedge + static_cast<GeometryIndex>(i);
                const auto target_vertex = static_cast<GeometryIndex>(vertices.size());
                vertices.push_back(vertex);
                vertex_it = copied_vertices.emplace(vertex_key, target_vertex).first;
            }
            auto halfedge = source_halfedge;
            halfedge.origin_vertex = vertex_it->second;
            halfedge.face = target_face_index;
            halfedge.next = first_halfedge + static_cast<GeometryIndex>((i + 1) % source_loop.size());
            halfedge.previous = first_halfedge
                + static_cast<GeometryIndex>((i + source_loop.size() - 1) % source_loop.size());
            halfedge.opposite = invalid_geometry_index;
            halfedge.radial_next = invalid_geometry_index;
            halfedge.id = unique_halfedge_id(halfedge.id);
            halfedge.edge_id = unique_edge_id(halfedge.edge_id);
            halfedges.push_back(halfedge);
        }
        auto face = source_face;
        face.halfedge = first_halfedge;
        face.id = unique_face_id(face.id);
        faces.push_back(face);
    }

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
    return CanonicalGeometry::create(std::move(vertices), std::move(halfedges), std::move(faces));
}

} // namespace

void GeometryPublicationLedger::set_actor_source(ActorId actor_id, CanonicalGeometryRef geometry)
{
    std::lock_guard<std::mutex> lock(mutex_);
    actor_sources_.emplace(std::move(actor_id), std::move(geometry));
}

PublicationCommitResult GeometryPublicationLedger::replace_scope(
    PublicationScopeKey scope,
    ActorId publication_actor_id,
    bool instruction_consumes_geometry,
    std::vector<GeometryItemEffect> effects)
{
    std::lock_guard<std::mutex> lock(mutex_);
    PublicationCommitResult result;
    std::stable_sort(effects.begin(), effects.end(),
        [](const GeometryItemEffect& left, const GeometryItemEffect& right) {
            return left.item_key < right.item_key;
        });
    for (auto& effect : effects) {
        if (!effect.succeeded) {
            if (!effect.consumed_faces.empty()) {
                result.diagnostics.push_back(PublicationDiagnostic{
                    PublicationDiagnosticCode::failed_item_attempted_consumption,
                    "Failed geometry item cannot consume source faces.", effect.item_key});
                effect.consumed_faces.clear();
            }
            effect.generated_geometry.reset();
            continue;
        }
        if (!instruction_consumes_geometry) effect.consumed_faces.clear();
        for (const auto& consumed : effect.consumed_faces) {
            const auto owner = actor_sources_.find(consumed.owner_actor_id);
            const bool found = owner != actor_sources_.end() && owner->second != nullptr
                && std::any_of(owner->second->faces().begin(), owner->second->faces().end(),
                    [&](const RuntimeFace& face) { return face.id == consumed.face_id; });
            if (!found) {
                result.diagnostics.push_back(PublicationDiagnostic{
                    PublicationDiagnosticCode::consumed_face_not_found,
                    "Consumed source face does not exist on its owning actor.", effect.item_key});
            }
        }
        if (instruction_consumes_geometry && effect.generated_geometry == nullptr
            && !effect.consumed_faces.empty() && !effect.allows_empty_replacement) {
            result.diagnostics.push_back(PublicationDiagnostic{
                PublicationDiagnosticCode::consuming_success_without_replacement,
                "Consuming item succeeded without replacement geometry.", effect.item_key});
        }
    }
    scopes_[std::move(scope)] = ScopeRecord{
        std::move(publication_actor_id), instruction_consumes_geometry, std::move(effects)};
    return result;
}

bool GeometryPublicationLedger::is_consumed(const SourceFaceIdentity& face) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& scope : scopes_) {
        if (!scope.second.consuming) continue;
        for (const auto& effect : scope.second.effects) {
            if (!effect.succeeded) continue;
            if (std::find_if(effect.consumed_faces.begin(), effect.consumed_faces.end(),
                    [&](const SourceFaceIdentity& candidate) {
                        return !(candidate < face) && !(face < candidate);
                    }) != effect.consumed_faces.end()) return true;
        }
    }
    return false;
}

/* Kept inline in assembly to avoid recursively locking is_consumed(). */
/* NOLINTNEXTLINE(readability-function-cognitive-complexity) */
CanonicalGeometryRef GeometryPublicationLedger::assemble_actor(const ActorId& actor_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto consumed = [&](const SourceFaceIdentity& face) {
    for (const auto& scope : scopes_) {
        if (!scope.second.consuming) continue;
        for (const auto& effect : scope.second.effects) {
            if (!effect.succeeded) continue;
            if (std::find_if(effect.consumed_faces.begin(), effect.consumed_faces.end(),
                    [&](const SourceFaceIdentity& candidate) {
                        return !(candidate < face) && !(face < candidate);
                    }) != effect.consumed_faces.end()) return true;
        }
    }
    return false;
    };
    std::vector<FaceCopy> copies;
    const auto source = actor_sources_.find(actor_id);
    if (source != actor_sources_.end() && source->second != nullptr) {
        for (GeometryIndex i = 0; i < source->second->faces().size(); ++i) {
            const SourceFaceIdentity identity{actor_id, source->second->faces()[i].id};
            if (!consumed(identity)) copies.push_back(FaceCopy{source->second.get(), i});
        }
    }
    for (const auto& scope : scopes_) {
        if (scope.second.publication_actor_id != actor_id) continue;
        for (const auto& effect : scope.second.effects) {
            if (!effect.succeeded || effect.generated_geometry == nullptr) continue;
            for (GeometryIndex i = 0; i < effect.generated_geometry->faces().size(); ++i) {
                copies.push_back(FaceCopy{effect.generated_geometry.get(), i});
            }
        }
    }
    RunElementIdAllocator allocator;
    return combine_faces(copies, allocator);
}

std::string to_string(PublicationDiagnosticCode code)
{
    switch (code) {
    case PublicationDiagnosticCode::failed_item_attempted_consumption: return "failed_item_attempted_consumption";
    case PublicationDiagnosticCode::consuming_success_without_replacement: return "consuming_success_without_replacement";
    case PublicationDiagnosticCode::consumed_face_not_found: return "consumed_face_not_found";
    case PublicationDiagnosticCode::generated_geometry_invalid: return "generated_geometry_invalid";
    }
    return "unknown";
}

} // namespace phoenix
