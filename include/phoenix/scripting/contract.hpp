#pragma once

#include "phoenix/common.hpp"
#include "phoenix/geometry.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace phoenix::scripting {

using Value = std::variant<std::int64_t, double, bool, std::string>;
using Bindings = std::map<std::string, Value>;
using LabelBindings = std::map<std::string, LabelId>;

enum class DiagnosticCode {
    empty_source,
    invalid_binding_name,
    duplicate_binding,
    compile_error,
    evaluation_error,
    unsupported_result,
    instruction_budget_exceeded,
    memory_budget_exceeded,
    cancelled,
    engine_unavailable,
};

struct Diagnostic {
    DiagnosticCode code = DiagnosticCode::evaluation_error;
    std::string message;
    std::optional<std::uint32_t> line;
    std::optional<std::uint32_t> column;
};

struct Limits {
    std::uint64_t instruction_budget = 100'000;
    std::uint64_t memory_bytes = 1U << 20U;
    std::uint32_t recursion_depth = 32;
};

struct Program {
    std::string language = "phoenix-js-expression";
    std::uint32_t language_version = 1;
    std::string source;
};

struct EvaluationRequest {
    Program program;
    Bindings global_bindings;
    Bindings local_bindings;
    Limits limits;
    SeedValue deterministic_seed = 0;
};

enum class EvaluationStatus {
    completed,
    rejected,
    failed,
    budget_exceeded,
    cancelled,
};

struct EvaluationResult {
    EvaluationStatus status = EvaluationStatus::failed;
    std::optional<Value> value;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool success() const noexcept;
};

struct ValidationResult {
    Bindings effective_bindings;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool success() const noexcept;
};

class CancellationToken {
public:
    virtual ~CancellationToken() = default;
    [[nodiscard]] virtual bool cancelled() const noexcept = 0;
};

class Engine {
public:
    virtual ~Engine() = default;
    [[nodiscard]] virtual std::string engine_id() const = 0;
    [[nodiscard]] virtual std::string engine_version() const = 0;
    [[nodiscard]] virtual EvaluationResult evaluate(
        const EvaluationRequest& request,
        const CancellationToken* cancellation = nullptr) const = 0;
};

enum class ScriptOutputKind {
    scalar,
    geometry,
    vertices,
    halfedges,
    faces,
};

struct ScriptGeometryInput {
    PortId port;
    CanonicalGeometryRef geometry;
    ActorId owner;
    std::optional<ScriptOutputKind> selection_kind;
    std::vector<std::uint64_t> selected_element_ids;
};

struct ScriptFace {
    std::vector<std::uint64_t> vertices;
    LabelId label = unassigned_label_id;
    std::vector<LabelId> directed_edge_labels;
};

struct ScriptMeshInspection {
    std::size_t vertices = 0, halfedges = 0, faces = 0;
    std::size_t border_halfedges = 0, border_edges = 0;
    bool empty = true, closed = false, pure_bivalent = false;
    bool pure_trivalent = false, pure_triangle = false, pure_quad = false;
};

struct ScriptElementInspection {
    ScriptOutputKind kind = ScriptOutputKind::vertices;
    std::uint64_t stable_id = 0;
    std::optional<Point3d> point;
    std::optional<LabelId> label;
    std::size_t degree = 0;
    bool bivalent = false, trivalent = false, triangle = false, quad = false;
    bool border = false, border_edge = false;
    std::optional<std::uint64_t> halfedge, opposite, next, prev;
    std::optional<std::uint64_t> next_on_vertex, prev_on_vertex;
    std::optional<std::uint64_t> source, target, face;
    std::vector<std::uint64_t> incident_halfedges;
};

// Invocation-local transactional geometry surface. Handles are valid only for
// one run; canonical input objects are immutable and never exposed for mutation.
class GeometryScriptHost {
public:
    virtual ~GeometryScriptHost() = default;
    [[nodiscard]] virtual std::vector<PortId> input_ports() const = 0;
    [[nodiscard]] virtual std::size_t vertex_count(const PortId& input) const = 0;
    [[nodiscard]] virtual std::size_t face_count(const PortId& input) const = 0;
    [[nodiscard]] virtual Point3d vertex(const PortId& input, std::size_t index) const = 0;
    [[nodiscard]] virtual ScriptFace face(const PortId& input, std::size_t index) const = 0;
    [[nodiscard]] virtual std::optional<ScriptOutputKind> input_selection_kind(
        const PortId& input) const = 0;
    [[nodiscard]] virtual std::vector<std::uint64_t> input_selection_handles(
        const PortId& input) = 0;
    [[nodiscard]] virtual std::uint64_t create_geometry(const PortId& output) = 0;
    [[nodiscard]] virtual std::uint64_t clone_geometry(
        const PortId& input, const PortId& output) = 0;
    [[nodiscard]] virtual std::size_t geometry_vertex_count(std::uint64_t geometry) const = 0;
    [[nodiscard]] virtual std::size_t geometry_face_count(std::uint64_t geometry) const = 0;
    [[nodiscard]] virtual Point3d geometry_vertex(
        std::uint64_t geometry, std::size_t index) const = 0;
    [[nodiscard]] virtual ScriptFace geometry_face(
        std::uint64_t geometry, std::size_t index) const = 0;
    virtual void set_vertex(std::uint64_t geometry, std::size_t index, Point3d point) = 0;
    [[nodiscard]] virtual std::size_t add_vertex(std::uint64_t geometry, Point3d point) = 0;
    virtual void set_face_label(std::uint64_t geometry, std::size_t face, LabelId label) = 0;
    virtual void set_directed_edge_label(std::uint64_t geometry,
        std::size_t face, std::size_t edge, LabelId label) = 0;
    virtual void add_face(std::uint64_t geometry, const ScriptFace& face) = 0;
    virtual void remove_face(std::uint64_t geometry, std::size_t face) = 0;
    [[nodiscard]] virtual std::vector<std::uint64_t> element_handles(
        std::uint64_t geometry,ScriptOutputKind kind) = 0;
    [[nodiscard]] virtual ScriptMeshInspection inspect_geometry(std::uint64_t geometry) = 0;
    [[nodiscard]] virtual ScriptElementInspection inspect_element(std::uint64_t element) = 0;
    [[nodiscard]] virtual std::uint64_t split_edge(std::uint64_t halfedge) = 0;
    [[nodiscard]] virtual std::uint64_t split_facet(
        std::uint64_t first, std::uint64_t second) = 0;
    [[nodiscard]] virtual std::uint64_t join_facet(std::uint64_t halfedge) = 0;
    [[nodiscard]] virtual std::uint64_t flip_edge(std::uint64_t halfedge) = 0;
    [[nodiscard]] virtual std::uint64_t make_hole(std::uint64_t halfedge) = 0;
    [[nodiscard]] virtual std::uint64_t fill_hole(std::uint64_t halfedge) = 0;
    [[nodiscard]] virtual std::uint64_t split_vertex(
        std::uint64_t first, std::uint64_t second) = 0;
    [[nodiscard]] virtual std::uint64_t join_vertex(std::uint64_t halfedge) = 0;
    [[nodiscard]] virtual std::uint64_t create_center_vertex(std::uint64_t halfedge) = 0;
    [[nodiscard]] virtual std::uint64_t erase_center_vertex(std::uint64_t halfedge) = 0;
    virtual void erase_connected_component(std::uint64_t halfedge) = 0;
    [[nodiscard]] virtual std::size_t keep_largest_connected_components(
        std::uint64_t geometry, std::size_t count) = 0;
    virtual void clear_geometry(std::uint64_t geometry) = 0;
    virtual void inside_out(std::uint64_t geometry) = 0;
    virtual void normalize_border(std::uint64_t geometry) = 0;
    [[nodiscard]] virtual std::uint64_t add_vertex_and_facet_to_border(
        std::uint64_t first, std::uint64_t second) = 0;
    [[nodiscard]] virtual std::uint64_t add_facet_to_border(
        std::uint64_t first, std::uint64_t second) = 0;
};

struct ScriptRequest {
    Program program{"phoenix-js-script", 1, {}};
    Bindings bindings;
    LabelBindings labels;
    std::vector<ScriptGeometryInput> geometry_inputs;
    struct LibraryAsset {
        std::string id;
        std::uint64_t version = 0;
        std::uint64_t content_fingerprint = 0;
        std::string source;
    };
    std::vector<LibraryAsset> libraries;
    Limits limits;
    SeedValue deterministic_seed = 0;
};

struct ScriptOutput {
    PortId port;
    ScriptOutputKind kind = ScriptOutputKind::scalar;
    std::optional<Value> scalar;
    std::uint64_t geometry = 0;
    std::vector<std::uint64_t> elements;
};

struct ScriptResult {
    EvaluationStatus status = EvaluationStatus::failed;
    std::vector<ScriptOutput> outputs;
    std::vector<Diagnostic> diagnostics;
    std::string console_output;

    [[nodiscard]] bool success() const noexcept;
};

class ScriptEngine : public Engine {
public:
    [[nodiscard]] virtual ScriptResult execute_script(const ScriptRequest& request,
        GeometryScriptHost& transaction,
        const CancellationToken* cancellation = nullptr) const = 0;
};

[[nodiscard]] ValidationResult validate_request(const EvaluationRequest& request);
[[nodiscard]] std::uint64_t cache_fingerprint(const EvaluationRequest& request,
    const std::string& engine_id, const std::string& engine_version) noexcept;
[[nodiscard]] std::string to_string(DiagnosticCode code);

} // namespace phoenix::scripting
