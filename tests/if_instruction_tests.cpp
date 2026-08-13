#include "phoenix/control/if_instruction.hpp"

#include <iostream>

namespace {

phoenix::InstructionExecutionFrame frame(phoenix::RuntimeValue condition)
{
    phoenix::InstructionExecutionFrame result;
    result.inputs.node_id = 11;
    result.inputs.promised_inputs = {
        {"input", phoenix::RuntimeValue::literal(phoenix::LiteralScalar{std::int64_t{42}})},
        {"condition", std::move(condition)},
    };
    return result;
}

bool routes_supported_scalars()
{
    using namespace phoenix;
    const auto handler = control::make_if_instruction_handler();
    const auto yes = handler(frame(RuntimeValue::literal(LiteralScalar{true})));
    const auto no = handler(frame(RuntimeValue::literal(LiteralScalar{std::int64_t{0}})));
    const auto number = handler(frame(RuntimeValue::literal(LiteralScalar{-0.25})));
    return yes.produced_outputs.size() == 1 && yes.produced_outputs[0].port == "then"
        && no.produced_outputs.size() == 1 && no.produced_outputs[0].port == "else"
        && number.produced_outputs.size() == 1
        && number.produced_outputs[0].port == "then";
}

bool rejects_missing_and_unsupported()
{
    using namespace phoenix;
    const auto handler = control::make_if_instruction_handler();
    const auto missing = handler(frame(RuntimeValue::missing()));
    const auto text = handler(frame(RuntimeValue::literal(LiteralScalar{std::string{"true"}})));
    return missing.produced_outputs.empty() && missing.failure_message.has_value()
        && text.produced_outputs.empty() && text.failure_message.has_value();
}

bool preserves_routed_value()
{
    using namespace phoenix;
    auto request = frame(RuntimeValue::literal(LiteralScalar{true}));
    const auto routed = control::make_if_instruction_handler()(request);
    const auto* literal = routed.produced_outputs[0].value.as_literal();
    const auto scalar = literal ? literal_first_scalar(*literal) : std::nullopt;
    return scalar && std::get<std::int64_t>(*scalar) == 42;
}

} // namespace

int main()
{
    const bool routing = routes_supported_scalars();
    const bool errors = rejects_missing_and_unsupported();
    const bool identity = preserves_routed_value();
    std::cout << "if typed truthiness routing: " << routing << '\n'
              << "if invalid condition rejection: " << errors << '\n'
              << "if value preservation: " << identity << '\n';
    return routing && errors && identity ? 0 : 1;
}
