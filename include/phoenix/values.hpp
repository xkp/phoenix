#pragma once

#include "phoenix/common.hpp"
#include "phoenix/geometry.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace phoenix {

struct EmptyValue final {
};

struct DefaultValue final {
    TypeId source_type;
};

struct GeometryValue {
    std::string debug_label;
    std::optional<ActorId> accumulation_actor_id;
    CanonicalGeometryRef geometry;
};

struct GeometryCollectionValue {
    std::vector<GeometryValue> contributions;
};

enum class GeometryElementKind { vertex, halfedge, face };

struct ElementSelectionValue {
    GeometryValue source;
    GeometryElementKind kind = GeometryElementKind::face;
    std::vector<std::uint64_t> element_ids;
};

using LiteralScalar = std::variant<std::int64_t, double, bool, std::string>;
using LiteralArray = std::vector<LiteralScalar>;
using LiteralValue = std::variant<LiteralScalar, LiteralArray>;

enum class LiteralScalarKind {
    integer,
    floating_point,
    boolean,
    string,
};

enum class ValuePresence {
    missing,
    present,
    empty,
    defaulted,
};

using RuntimePayload = std::variant<GeometryValue, GeometryCollectionValue,
    ElementSelectionValue, LiteralValue, EmptyValue, DefaultValue>;

struct RuntimeValue {
    ValuePresence presence = ValuePresence::missing;
    RuntimePayload payload = EmptyValue{};

    [[nodiscard]] static RuntimeValue missing();
    [[nodiscard]] static RuntimeValue empty();
    [[nodiscard]] static RuntimeValue geometry(
        std::string debug_label,
        std::optional<ActorId> accumulation_actor_id = std::nullopt);
    [[nodiscard]] static RuntimeValue geometry(
        CanonicalGeometryRef geometry,
        std::string debug_label = {},
        std::optional<ActorId> accumulation_actor_id = std::nullopt);
    [[nodiscard]] static RuntimeValue geometry_collection(std::vector<GeometryValue> contributions);
    [[nodiscard]] static RuntimeValue element_selection(ElementSelectionValue selection);
    [[nodiscard]] static RuntimeValue literal(LiteralValue value);
    [[nodiscard]] static RuntimeValue defaulted(TypeId source_type);

    [[nodiscard]] bool is_missing() const noexcept;
    [[nodiscard]] bool is_present() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_defaulted() const noexcept;
    [[nodiscard]] bool is_geometry() const noexcept;
    [[nodiscard]] bool is_geometry_collection() const noexcept;
    [[nodiscard]] bool is_literal() const noexcept;
    [[nodiscard]] bool is_element_selection() const noexcept;

    [[nodiscard]] const GeometryValue* as_geometry() const noexcept;
    [[nodiscard]] const GeometryCollectionValue* as_geometry_collection() const noexcept;
    [[nodiscard]] const LiteralValue* as_literal() const noexcept;
    [[nodiscard]] const ElementSelectionValue* as_element_selection() const noexcept;
    [[nodiscard]] const DefaultValue* as_default() const noexcept;
};

[[nodiscard]] LiteralScalarKind literal_scalar_kind(const LiteralScalar& value) noexcept;
[[nodiscard]] std::optional<LiteralScalar> literal_first_scalar(const LiteralValue& value) noexcept;

enum class PortFulfillment {
    unfulfilled,
    fulfilled,
};

enum class PortExpectation {
    unpromised,
    promised,
};

struct PortValue {
    PortId port;
    RuntimeValue value;

    [[nodiscard]] PortFulfillment fulfillment() const noexcept;
};

struct PortState {
    PortId port;
    PortExpectation expectation = PortExpectation::unpromised;
    RuntimeValue value = RuntimeValue::missing();

    [[nodiscard]] bool is_promised() const noexcept;
    [[nodiscard]] bool is_fulfilled() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;
};

struct InputSetState {
    std::vector<PortState> ports;

    [[nodiscard]] bool all_promised_ports_fulfilled() const noexcept;
    [[nodiscard]] bool any_promised_port_fulfilled() const noexcept;
    [[nodiscard]] std::size_t promised_port_count() const noexcept;
    [[nodiscard]] std::size_t fulfilled_promised_port_count() const noexcept;
};

struct GeometryAggregationInput {
    PortId port;
    std::vector<GeometryValue> contributions;
};

enum class GeometryAggregationStatus {
    aggregated,
    owner_conflict,
};

struct VirtualGeometry {
    PortId port;
    std::vector<GeometryValue> contributions;
    std::optional<ActorId> accumulation_actor_id;
    bool materialized = false;
    GeometryAggregationStatus status = GeometryAggregationStatus::aggregated;
};

class GeometryAggregator {
public:
    [[nodiscard]] VirtualGeometry aggregate(const GeometryAggregationInput& input) const;
};

[[nodiscard]] std::string to_string(ValuePresence presence);
[[nodiscard]] std::string to_string(PortFulfillment fulfillment);
[[nodiscard]] std::string to_string(PortExpectation expectation);

} // namespace phoenix
