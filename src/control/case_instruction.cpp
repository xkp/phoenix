#include "phoenix/control/case_instruction.hpp"

#include "phoenix/control/if_instruction.hpp"
#include "phoenix/scripting/quickjs_engine.hpp"

#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

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

TruthinessResult expression_truthiness(const scripting::Value& value)
{
    const auto literal = std::visit([](const auto& item) -> LiteralScalar {
        return item;
    }, value);
    return resolve_truthiness(RuntimeValue::literal(LiteralValue{literal}));
}

} // namespace

std::string configuration_revision(const CaseInstructionConfig& config)
{
    const auto engine = config.expression_engine
        ? config.expression_engine
        : std::make_shared<scripting::QuickJsEngine>();
    std::ostringstream stream;
    stream << "case-v1|" << config.input_port << '|' << config.else_port;
    for (const auto& branch : config.branches)
        stream << '|' << branch.output_port << '|'
               << scripting::expression_configuration_revision(branch.expression, *engine);
    if(config.geometry_bindings){stream<<"|geometry|"<<static_cast<int>(config.geometry_bindings->element_kind);for(const auto& b:config.geometry_bindings->bindings)stream<<'|'<<b.variable<<':'<<static_cast<int>(b.kind)<<':'<<(b.label1?b.label1->value():-1)<<':'<<(b.label2?b.label2->value():-1)<<':'<<(b.label3?b.label3->value():-1)<<':'<<(b.label4?b.label4->value():-1)<<':'<<static_cast<int>(b.choice)<<':'<<static_cast<int>(b.relation);}
    return stream.str();
}

InstructionHandler make_case_instruction_handler(CaseInstructionConfig config)
{
    if (!config.expression_engine)
        config.expression_engine = std::make_shared<scripting::QuickJsEngine>();
    return [config = std::move(config)](const InstructionExecutionFrame& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        const auto* routed = input(frame, config.input_port);
        if (!routed || routed->is_missing()) {
            result.failure_message = "Case requires an input value.";
            return result;
        }
        if (config.branches.empty()) {
            result.failure_message = "Case requires at least one expression branch.";
            return result;
        }
        scripting::Bindings bindings;
        if (frame.function_variables) bindings = *frame.function_variables;
        for (const auto& candidate : frame.inputs.promised_inputs) {
            if (candidate.port == config.input_port) continue;
            if (const auto value = scalar(candidate.value)) bindings[candidate.port] = *value;
        }
        std::set<PortId> outputs;
        for (const auto& branch : config.branches) {
            if (branch.output_port.empty() || branch.output_port == config.else_port
                || !outputs.insert(branch.output_port).second) {
                result.failure_message = "Case branch output ports must be non-empty and unique.";
                return result;
            }
        }
        if(config.geometry_bindings){const auto inputs=scripting::resolve_geometry_binding_inputs(*routed,config.geometry_bindings->element_kind);if(inputs.empty()){result.failure_message="Case geometry bindings require matching canonical geometry or element selection input.";return result;}for(const auto& bound:inputs){std::map<PortId,std::vector<std::uint64_t>> routed_ids;for(const auto element:bound.elements){auto locals=bindings;try{const auto derived=scripting::evaluate_geometry_bindings(*bound.source.geometry,config.geometry_bindings->element_kind,element,config.geometry_bindings->bindings,frame.effective_seed.value_or(frame.context.global_seed));for(const auto& item:derived)locals[item.first]=item.second;}catch(const std::exception& error){result.failure_message=error.what();return result;}PortId output=config.else_port;for(const auto& branch:config.branches){const auto evaluated=scripting::evaluate_expression(*config.expression_engine,branch.expression,locals,frame.effective_seed.value_or(frame.context.global_seed));if(!evaluated.success()){result.failure_message=scripting::format_diagnostics(evaluated);return result;}const auto truth=expression_truthiness(*evaluated.value);if(truth.status==TruthinessStatus::resolved&&truth.value){output=branch.output_port;break;}}routed_ids[output].push_back(scripting::stable_element_id(*bound.source.geometry,config.geometry_bindings->element_kind,element));}for(auto& item:routed_ids){ElementSelectionValue selection{bound.source,scripting::selection_kind(config.geometry_bindings->element_kind),std::move(item.second)};result.produced_outputs.push_back({item.first,RuntimeValue::element_selection(std::move(selection))});}}return result;
        }
        for(const auto& branch:config.branches){const auto evaluated=scripting::evaluate_expression(*config.expression_engine,branch.expression,bindings,frame.effective_seed.value_or(frame.context.global_seed));if(!evaluated.success()){result.failure_message=scripting::format_diagnostics(evaluated);return result;}const auto truth=expression_truthiness(*evaluated.value);if(truth.status==TruthinessStatus::resolved&&truth.value){result.produced_outputs.push_back({branch.output_port,*routed});return result;}}
        result.produced_outputs.push_back({config.else_port, *routed});
        return result;
    };
}

} // namespace phoenix::control
