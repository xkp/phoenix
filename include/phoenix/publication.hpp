#pragma once

#include "phoenix/geometry.hpp"

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace phoenix {

struct SourceFaceIdentity {
    ActorId owner_actor_id;
    FaceId face_id;

    friend bool operator<(const SourceFaceIdentity& left, const SourceFaceIdentity& right)
    {
        if (left.owner_actor_id != right.owner_actor_id) {
            return left.owner_actor_id < right.owner_actor_id;
        }
        return left.face_id.value() < right.face_id.value();
    }
};

struct GeometryItemEffect {
    std::uint64_t item_key = 0;
    bool succeeded = true;
    CanonicalGeometryRef generated_geometry;
    std::vector<SourceFaceIdentity> consumed_faces;
    std::string failure_message;
};

struct PublicationScopeKey {
    FunctionId function_id;
    FunctionCallPath call_path;
    NodeId node_id = 0;

    friend bool operator<(const PublicationScopeKey& left, const PublicationScopeKey& right)
    {
        if (left.function_id != right.function_id) return left.function_id < right.function_id;
        if (left.call_path != right.call_path) return left.call_path < right.call_path;
        return left.node_id < right.node_id;
    }
};

enum class PublicationDiagnosticCode {
    failed_item_attempted_consumption,
    consuming_success_without_replacement,
    consumed_face_not_found,
    generated_geometry_invalid,
};

struct PublicationDiagnostic {
    PublicationDiagnosticCode code;
    std::string message;
    std::optional<std::uint64_t> item_key;
};

struct PublicationCommitResult {
    std::vector<PublicationDiagnostic> diagnostics;
};

class GeometryPublicationLedger {
public:
    void set_actor_source(ActorId actor_id, CanonicalGeometryRef geometry);

    [[nodiscard]] PublicationCommitResult replace_scope(
        PublicationScopeKey scope,
        ActorId publication_actor_id,
        bool instruction_consumes_geometry,
        std::vector<GeometryItemEffect> effects);

    [[nodiscard]] CanonicalGeometryRef assemble_actor(const ActorId& actor_id) const;
    [[nodiscard]] bool is_consumed(const SourceFaceIdentity& face) const;

private:
    struct ScopeRecord {
        ActorId publication_actor_id;
        bool consuming = false;
        std::vector<GeometryItemEffect> effects;
    };

    std::map<ActorId, CanonicalGeometryRef> actor_sources_;
    std::map<PublicationScopeKey, ScopeRecord> scopes_;
    mutable std::mutex mutex_;
};

[[nodiscard]] std::string to_string(PublicationDiagnosticCode code);

} // namespace phoenix
