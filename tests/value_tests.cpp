#include "phoenix/values.hpp"

#include <cstdlib>
#include <iostream>

namespace {

bool test_missing_value()
{
    const auto value = phoenix::RuntimeValue::missing();
    return value.is_missing() && !value.is_present();
}

bool test_empty_value()
{
    const auto value = phoenix::RuntimeValue::empty();
    return value.is_empty() && !value.is_missing();
}

bool test_geometry_value()
{
    const auto value = phoenix::RuntimeValue::geometry("window_mesh");
    const auto* geometry = value.as_geometry();
    return value.is_present() && value.is_geometry() && geometry != nullptr
        && geometry->debug_label == "window_mesh";
}

bool test_literal_value()
{
    const auto value = phoenix::RuntimeValue::literal(phoenix::LiteralValue{std::int64_t{7}});
    const auto* literal = value.as_literal();
    return value.is_present() && value.is_literal() && literal != nullptr;
}

bool test_literal_kind()
{
    const auto kind = phoenix::literal_scalar_kind(phoenix::LiteralScalar{std::string{"x"}});
    return kind == phoenix::LiteralScalarKind::string;
}

bool test_literal_first_scalar_from_array()
{
    const phoenix::LiteralValue value = phoenix::LiteralArray{
        phoenix::LiteralScalar{std::int64_t{3}},
        phoenix::LiteralScalar{std::int64_t{4}},
    };

    const auto first = phoenix::literal_first_scalar(value);
    return first.has_value() && std::holds_alternative<std::int64_t>(*first)
        && std::get<std::int64_t>(*first) == 3;
}

bool test_defaulted_value()
{
    const auto value = phoenix::RuntimeValue::defaulted("int");
    const auto* default_value = value.as_default();
    return value.is_defaulted() && default_value != nullptr && default_value->source_type == "int";
}

bool test_port_fulfillment()
{
    const phoenix::PortValue missing_port{"input", phoenix::RuntimeValue::missing()};
    const phoenix::PortValue present_port{"input", phoenix::RuntimeValue::geometry("mesh")};

    return missing_port.fulfillment() == phoenix::PortFulfillment::unfulfilled
        && present_port.fulfillment() == phoenix::PortFulfillment::fulfilled;
}

bool test_port_state_readiness()
{
    phoenix::PortState promised_missing;
    promised_missing.port = "a";
    promised_missing.expectation = phoenix::PortExpectation::promised;
    promised_missing.value = phoenix::RuntimeValue::missing();

    phoenix::PortState promised_present;
    promised_present.port = "b";
    promised_present.expectation = phoenix::PortExpectation::promised;
    promised_present.value = phoenix::RuntimeValue::geometry("mesh");

    phoenix::PortState unpromised_missing;
    unpromised_missing.port = "c";
    unpromised_missing.expectation = phoenix::PortExpectation::unpromised;
    unpromised_missing.value = phoenix::RuntimeValue::missing();

    return !promised_missing.is_ready()
        && promised_present.is_ready()
        && unpromised_missing.is_ready();
}

bool test_input_set_state()
{
    phoenix::InputSetState input_state;

    phoenix::PortState promised_present;
    promised_present.port = "a";
    promised_present.expectation = phoenix::PortExpectation::promised;
    promised_present.value = phoenix::RuntimeValue::geometry("mesh_a");

    phoenix::PortState promised_missing;
    promised_missing.port = "b";
    promised_missing.expectation = phoenix::PortExpectation::promised;
    promised_missing.value = phoenix::RuntimeValue::missing();

    phoenix::PortState unpromised_missing;
    unpromised_missing.port = "c";
    unpromised_missing.expectation = phoenix::PortExpectation::unpromised;
    unpromised_missing.value = phoenix::RuntimeValue::missing();

    input_state.ports = {promised_present, promised_missing, unpromised_missing};

    return input_state.promised_port_count() == 2
        && input_state.fulfilled_promised_port_count() == 1
        && input_state.any_promised_port_fulfilled()
        && !input_state.all_promised_ports_fulfilled();
}

bool test_geometry_aggregation()
{
    phoenix::GeometryAggregationInput input;
    input.port = "mesh";
    input.contributions = {
        phoenix::GeometryValue{"a"},
        phoenix::GeometryValue{"b"},
    };

    const phoenix::GeometryAggregator aggregator;
    const auto aggregate = aggregator.aggregate(input);

    return aggregate.port == "mesh"
        && aggregate.contributions.size() == 2
        && !aggregate.materialized;
}

bool run_test(const char* name, bool (*test_fn)())
{
    const bool passed = test_fn();
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << '\n';
    return passed;
}

} // namespace

int main()
{
    bool ok = true;

    ok = run_test("missing value", test_missing_value) && ok;
    ok = run_test("empty value", test_empty_value) && ok;
    ok = run_test("geometry value", test_geometry_value) && ok;
    ok = run_test("literal value", test_literal_value) && ok;
    ok = run_test("literal kind", test_literal_kind) && ok;
    ok = run_test("literal first scalar from array", test_literal_first_scalar_from_array) && ok;
    ok = run_test("defaulted value", test_defaulted_value) && ok;
    ok = run_test("port fulfillment", test_port_fulfillment) && ok;
    ok = run_test("port state readiness", test_port_state_readiness) && ok;
    ok = run_test("input set state", test_input_set_state) && ok;
    ok = run_test("geometry aggregation", test_geometry_aggregation) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
