#pragma once

#include "phoenix/scripting/contract.hpp"
#include "phoenix/values.hpp"

#include <optional>

namespace phoenix::scripting {

enum class BindingElementKind { face, halfedge };
enum class GeometryBindingKind {
    length,
    area,
    border_edge,
    count_edge_labels,
    count_face_labels,
    opposite_edge,
    adjacent,
    angle,
    distance,
    opposite_extended,
};
enum class BindingChoice { any, largest, shortest };
enum class BindingRelation { any, previous, next };

struct GeometryBindingSpec {
    std::string variable;
    GeometryBindingKind kind = GeometryBindingKind::length;
    std::optional<LabelId> label1;
    std::optional<LabelId> label2;
    BindingChoice choice = BindingChoice::any;
    BindingRelation relation = BindingRelation::any;
    std::optional<LabelId> label3;
    std::optional<LabelId> label4;
};

struct GeometryBindingPlan {
    BindingElementKind element_kind = BindingElementKind::face;
    std::vector<GeometryBindingSpec> bindings;
};

struct GeometryBindingInput {
    GeometryValue source;
    std::vector<GeometryIndex> elements;
};

[[nodiscard]] std::optional<GeometryBindingInput> resolve_geometry_binding_input(
    const RuntimeValue& value, BindingElementKind kind);
[[nodiscard]] std::vector<GeometryBindingInput> resolve_geometry_binding_inputs(
    const RuntimeValue& value, BindingElementKind kind);
[[nodiscard]] Bindings evaluate_geometry_bindings(const CanonicalGeometry& geometry,
    BindingElementKind kind, GeometryIndex element,
    const std::vector<GeometryBindingSpec>& bindings, SeedValue seed);
[[nodiscard]] GeometryElementKind selection_kind(BindingElementKind kind) noexcept;
[[nodiscard]] std::uint64_t stable_element_id(const CanonicalGeometry& geometry,
    BindingElementKind kind, GeometryIndex element);

} // namespace phoenix::scripting
