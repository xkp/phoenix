#include "phoenix/control/if_instruction.hpp"
#include "phoenix/scripting/quickjs_engine.hpp"

#include <utility>
#include <sstream>

namespace phoenix::control {
namespace {

const RuntimeValue* input(const InstructionExecutionFrame& frame, const PortId& port)
{
    for (const auto& candidate : frame.inputs.promised_inputs)
        if (candidate.port == port) return &candidate.value;
    return nullptr;
}

std::optional<scripting::Value> scalar(const RuntimeValue& value)
{
    const auto* literal = value.as_literal();
    if (!literal) return {};
    const auto first = literal_first_scalar(*literal);
    if (!first) return {};
    return std::visit([](const auto& item) -> scripting::Value { return item; }, *first);
}

} // namespace

TruthinessResult resolve_truthiness(const RuntimeValue& value) noexcept
{
    if (value.is_missing() || value.is_empty() || value.is_defaulted())
        return {TruthinessStatus::missing, false};
    const auto* literal = value.as_literal();
    if (!literal) return {TruthinessStatus::unsupported, false};
    const auto scalar = literal_first_scalar(*literal);
    if (!scalar) return {TruthinessStatus::unsupported, false};
    if (const auto* boolean = std::get_if<bool>(&*scalar))
        return {TruthinessStatus::resolved, *boolean};
    if (const auto* integer = std::get_if<std::int64_t>(&*scalar))
        return {TruthinessStatus::resolved, *integer != 0};
    if (const auto* number = std::get_if<double>(&*scalar))
        return {TruthinessStatus::resolved, *number != 0.0};
    return {TruthinessStatus::unsupported, false};
}

std::string configuration_revision(const IfInstructionConfig& config)
{
    const auto engine=config.expression_engine?config.expression_engine:std::make_shared<scripting::QuickJsEngine>();std::ostringstream stream;stream<<"if-v1|"<<config.input_port<<'|'<<config.condition_port<<'|'<<config.then_port<<'|'<<config.else_port;if(config.expression)stream<<'|'<<scripting::expression_configuration_revision(*config.expression,*engine);if(config.geometry_bindings){stream<<"|geometry|"<<static_cast<int>(config.geometry_bindings->element_kind);for(const auto& b:config.geometry_bindings->bindings)stream<<'|'<<b.variable<<':'<<static_cast<int>(b.kind)<<':'<<(b.label1?b.label1->value():-1)<<':'<<(b.label2?b.label2->value():-1)<<':'<<(b.label3?b.label3->value():-1)<<':'<<(b.label4?b.label4->value():-1)<<':'<<static_cast<int>(b.choice)<<':'<<static_cast<int>(b.relation);}return stream.str();
}

InstructionHandler make_if_instruction_handler(IfInstructionConfig config)
{
    if (config.expression && !config.expression_engine)
        config.expression_engine = std::make_shared<scripting::QuickJsEngine>();
    return [config = std::move(config)](const InstructionExecutionFrame& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        const auto* routed = input(frame, config.input_port);
        const auto* condition = input(frame, config.condition_port);
        if (!routed || routed->is_missing()) {
            result.failure_message = "If requires an input value.";
            return result;
        }
        if (!config.expression && !condition) {
            result.failure_message = "If requires a condition value.";
            return result;
        }
        if(config.geometry_bindings){if(!config.expression){result.failure_message="If geometry bindings require an expression.";return result;}const auto inputs=scripting::resolve_geometry_binding_inputs(*routed,config.geometry_bindings->element_kind);if(inputs.empty()){result.failure_message="If geometry bindings require matching canonical geometry or element selection input.";return result;}scripting::Bindings base;for(const auto& candidate:frame.inputs.promised_inputs){if(candidate.port==config.input_port)continue;if(const auto value=scalar(candidate.value))base[candidate.port]=*value;}for(const auto& bound:inputs){std::vector<std::uint64_t> yes,no;for(const auto element:bound.elements){auto locals=base;try{const auto derived=scripting::evaluate_geometry_bindings(*bound.source.geometry,config.geometry_bindings->element_kind,element,config.geometry_bindings->bindings,frame.effective_seed.value_or(frame.context.global_seed));for(const auto& item:derived)locals[item.first]=item.second;}catch(const std::exception& error){result.failure_message=error.what();return result;}const auto evaluated=scripting::evaluate_expression(*config.expression_engine,*config.expression,std::move(locals),frame.effective_seed.value_or(frame.context.global_seed));if(!evaluated.success()){result.failure_message=scripting::format_diagnostics(evaluated);return result;}const auto literal=std::visit([](const auto& item)->LiteralScalar{return item;},*evaluated.value);const auto truth=resolve_truthiness(RuntimeValue::literal(LiteralValue{literal}));const auto id=scripting::stable_element_id(*bound.source.geometry,config.geometry_bindings->element_kind,element);if(truth.status==TruthinessStatus::resolved&&truth.value)yes.push_back(id);else no.push_back(id);}auto publish=[&](const PortId& port,std::vector<std::uint64_t> ids){if(ids.empty())return;ElementSelectionValue selection{bound.source,scripting::selection_kind(config.geometry_bindings->element_kind),std::move(ids)};result.produced_outputs.push_back({port,RuntimeValue::element_selection(std::move(selection))});};publish(config.then_port,std::move(yes));publish(config.else_port,std::move(no));}return result;}
        TruthinessResult truth;
        if (config.expression) {
            scripting::Bindings bindings;
            for (const auto& candidate : frame.inputs.promised_inputs) {
                if (candidate.port == config.input_port) continue;
                if (const auto value = scalar(candidate.value)) bindings[candidate.port] = *value;
            }
            const auto evaluated = scripting::evaluate_expression(*config.expression_engine,
                *config.expression, std::move(bindings),
                frame.effective_seed.value_or(frame.context.global_seed));
            if (!evaluated.success()) {
                result.failure_message = scripting::format_diagnostics(evaluated);
                return result;
            }
            const auto literal = std::visit([](const auto& item) -> LiteralScalar {
                return item;
            }, *evaluated.value);
            truth = resolve_truthiness(RuntimeValue::literal(LiteralValue{literal}));
        } else {
            truth = resolve_truthiness(*condition);
        }
        if (truth.status != TruthinessStatus::resolved) {
            result.failure_message = truth.status == TruthinessStatus::missing
                ? "If condition is missing."
                : "If condition must be a boolean or numeric scalar.";
            return result;
        }
        result.produced_outputs.push_back({truth.value ? config.then_port : config.else_port,
            *routed});
        return result;
    };
}

} // namespace phoenix::control
