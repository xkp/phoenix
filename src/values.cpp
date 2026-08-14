#include "phoenix/values.hpp"

#include <utility>

namespace phoenix {
namespace {

std::optional<ActorId> common_owner(const std::vector<GeometryValue>& contributions)
{
    std::optional<ActorId> owner;
    for (const auto& contribution : contributions) {
        if (!contribution.accumulation_actor_id.has_value()) {
            continue;
        }

        if (!owner.has_value()) {
            owner = contribution.accumulation_actor_id;
            continue;
        }

        if (*owner != *contribution.accumulation_actor_id) {
            return std::nullopt;
        }
    }

    return owner;
}

bool has_owner_conflict(const std::vector<GeometryValue>& contributions)
{
    std::optional<ActorId> owner;
    for (const auto& contribution : contributions) {
        if (!contribution.accumulation_actor_id.has_value()) {
            continue;
        }

        if (!owner.has_value()) {
            owner = contribution.accumulation_actor_id;
            continue;
        }

        if (*owner != *contribution.accumulation_actor_id) {
            return true;
        }
    }

    return false;
}

} // namespace

LiteralScalarKind literal_scalar_kind(const LiteralScalar& value) noexcept
{
    if (std::holds_alternative<std::int64_t>(value)) {
        return LiteralScalarKind::integer;
    }
    if (std::holds_alternative<double>(value)) {
        return LiteralScalarKind::floating_point;
    }
    if (std::holds_alternative<bool>(value)) {
        return LiteralScalarKind::boolean;
    }

    return LiteralScalarKind::string;
}

std::optional<LiteralScalar> literal_first_scalar(const LiteralValue& value) noexcept
{
    if (const auto* scalar = std::get_if<LiteralScalar>(&value)) {
        return *scalar;
    }

    const auto* array = std::get_if<LiteralArray>(&value);
    if (array == nullptr || array->empty()) {
        return std::nullopt;
    }

    return array->front();
}

RuntimeValue RuntimeValue::missing()
{
    RuntimeValue value;
    value.presence = ValuePresence::missing;
    value.payload = EmptyValue{};
    return value;
}

RuntimeValue RuntimeValue::empty()
{
    RuntimeValue value;
    value.presence = ValuePresence::empty;
    value.payload = EmptyValue{};
    return value;
}

RuntimeValue RuntimeValue::geometry(
    std::string debug_label,
    std::optional<ActorId> accumulation_actor_id)
{
    RuntimeValue value;
    value.presence = ValuePresence::present;
    value.payload = GeometryValue{std::move(debug_label), std::move(accumulation_actor_id), {}};
    return value;
}

RuntimeValue RuntimeValue::geometry(
    CanonicalGeometryRef geometry,
    std::string debug_label,
    std::optional<ActorId> accumulation_actor_id)
{
    RuntimeValue value;
    value.presence = ValuePresence::present;
    value.payload = GeometryValue{
        std::move(debug_label), std::move(accumulation_actor_id), std::move(geometry)};
    return value;
}

RuntimeValue RuntimeValue::geometry_collection(std::vector<GeometryValue> contributions)
{
    RuntimeValue value;
    value.presence = ValuePresence::present;
    value.payload = GeometryCollectionValue{std::move(contributions)};
    return value;
}

RuntimeValue RuntimeValue::element_selection(ElementSelectionValue selection)
{
    RuntimeValue value;
    value.presence = ValuePresence::present;
    value.payload = std::move(selection);
    return value;
}

RuntimeValue RuntimeValue::literal(LiteralValue literal_value)
{
    RuntimeValue value;
    value.presence = ValuePresence::present;
    value.payload = std::move(literal_value);
    return value;
}

RuntimeValue RuntimeValue::defaulted(TypeId source_type)
{
    RuntimeValue value;
    value.presence = ValuePresence::defaulted;
    value.payload = DefaultValue{std::move(source_type)};
    return value;
}

bool RuntimeValue::is_missing() const noexcept
{
    return presence == ValuePresence::missing;
}

bool RuntimeValue::is_present() const noexcept
{
    return presence == ValuePresence::present;
}

bool RuntimeValue::is_empty() const noexcept
{
    return presence == ValuePresence::empty;
}

bool RuntimeValue::is_defaulted() const noexcept
{
    return presence == ValuePresence::defaulted;
}

bool RuntimeValue::is_geometry() const noexcept
{
    return std::holds_alternative<GeometryValue>(payload);
}

bool RuntimeValue::is_geometry_collection() const noexcept
{
    return std::holds_alternative<GeometryCollectionValue>(payload);
}

bool RuntimeValue::is_literal() const noexcept
{
    return std::holds_alternative<LiteralValue>(payload);
}

bool RuntimeValue::is_element_selection() const noexcept
{
    return std::holds_alternative<ElementSelectionValue>(payload);
}

const GeometryValue* RuntimeValue::as_geometry() const noexcept
{
    return std::get_if<GeometryValue>(&payload);
}

const GeometryCollectionValue* RuntimeValue::as_geometry_collection() const noexcept
{
    return std::get_if<GeometryCollectionValue>(&payload);
}

const LiteralValue* RuntimeValue::as_literal() const noexcept
{
    return std::get_if<LiteralValue>(&payload);
}

const ElementSelectionValue* RuntimeValue::as_element_selection() const noexcept
{
    return std::get_if<ElementSelectionValue>(&payload);
}

const DefaultValue* RuntimeValue::as_default() const noexcept
{
    return std::get_if<DefaultValue>(&payload);
}

PortFulfillment PortValue::fulfillment() const noexcept
{
    return value.is_missing() ? PortFulfillment::unfulfilled : PortFulfillment::fulfilled;
}

bool PortState::is_promised() const noexcept
{
    return expectation == PortExpectation::promised;
}

bool PortState::is_fulfilled() const noexcept
{
    return value.is_missing() ? false : true;
}

bool PortState::is_ready() const noexcept
{
    return !is_promised() || is_fulfilled();
}

bool InputSetState::all_promised_ports_fulfilled() const noexcept
{
    for (const auto& port : ports) {
        if (port.is_promised() && !port.is_fulfilled()) {
            return false;
        }
    }

    return true;
}

bool InputSetState::any_promised_port_fulfilled() const noexcept
{
    for (const auto& port : ports) {
        if (port.is_promised() && port.is_fulfilled()) {
            return true;
        }
    }

    return false;
}

std::size_t InputSetState::promised_port_count() const noexcept
{
    std::size_t count = 0;
    for (const auto& port : ports) {
        if (port.is_promised()) {
            ++count;
        }
    }

    return count;
}

std::size_t InputSetState::fulfilled_promised_port_count() const noexcept
{
    std::size_t count = 0;
    for (const auto& port : ports) {
        if (port.is_promised() && port.is_fulfilled()) {
            ++count;
        }
    }

    return count;
}

VirtualGeometry GeometryAggregator::aggregate(const GeometryAggregationInput& input) const
{
    const bool owner_conflict = has_owner_conflict(input.contributions);
    return VirtualGeometry{
        input.port,
        input.contributions,
        owner_conflict ? std::nullopt : common_owner(input.contributions),
        false,
        owner_conflict ? GeometryAggregationStatus::owner_conflict
                       : GeometryAggregationStatus::aggregated,
    };
}

std::string to_string(ValuePresence presence)
{
    switch (presence) {
    case ValuePresence::missing:
        return "missing";
    case ValuePresence::present:
        return "present";
    case ValuePresence::empty:
        return "empty";
    case ValuePresence::defaulted:
        return "defaulted";
    }

    return "unknown";
}

std::string to_string(PortFulfillment fulfillment)
{
    switch (fulfillment) {
    case PortFulfillment::unfulfilled:
        return "unfulfilled";
    case PortFulfillment::fulfilled:
        return "fulfilled";
    }

    return "unknown";
}

std::string to_string(PortExpectation expectation)
{
    switch (expectation) {
    case PortExpectation::unpromised:
        return "unpromised";
    case PortExpectation::promised:
        return "promised";
    }

    return "unknown";
}

} // namespace phoenix
