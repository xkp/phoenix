#include "phoenix/control/if_instruction.hpp"
#include "phoenix/scripting/quickjs_engine.hpp"

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

bool routes_sandboxed_expression()
{
    using namespace phoenix;
    control::IfInstructionConfig config;
    scripting::ExpressionSpec expression;
    expression.program.source =
        "(() => { threshold = 99; return condition > threshold && typeof td === 'undefined'; })()";
    expression.global_bindings["threshold"] = std::int64_t{3};
    config.expression = expression;
    const auto handler = control::make_if_instruction_handler(config);
    const auto yes = handler(frame(RuntimeValue::literal(LiteralScalar{std::int64_t{4}})));
    const auto no = handler(frame(RuntimeValue::literal(LiteralScalar{std::int64_t{2}})));
    return yes.produced_outputs.size() == 1 && yes.produced_outputs[0].port == "then"
        && no.produced_outputs.size() == 1 && no.produced_outputs[0].port == "else";
}

bool reports_expression_failure_and_stable_identity()
{
    using namespace phoenix;
    auto engine = std::make_shared<scripting::QuickJsEngine>();
    scripting::ExpressionSpec first;
    first.program.source = "condition > 0";
    auto second = first;
    second.program.source = "condition >= 0";
    const auto revision = scripting::expression_configuration_revision(first, *engine);
    const auto changed = scripting::expression_configuration_revision(second, *engine);
    control::IfInstructionConfig config;
    config.expression = scripting::ExpressionSpec{};
    config.expression->program.source = "condition +";
    config.expression_engine = engine;
    const auto failed = control::make_if_instruction_handler(config)(
        frame(RuntimeValue::literal(LiteralScalar{std::int64_t{1}})));
    return !revision.empty() && revision != changed
        && failed.produced_outputs.empty() && failed.failure_message.has_value();
}

bool routes_geometry_derived_binding()
{
    using namespace phoenix;
    auto geometry=CanonicalGeometry::create(
        {{{0,0,0},VertexId{1},0},{{2,0,0},VertexId{2},1},{{0,2,0},VertexId{3},2}},
        {{0,0,1,2,invalid_geometry_index,invalid_geometry_index,HalfedgeId{10},EdgeId{20},LabelId{30}},
         {1,0,2,0,invalid_geometry_index,invalid_geometry_index,HalfedgeId{11},EdgeId{21},LabelId{31}},
         {2,0,0,1,invalid_geometry_index,invalid_geometry_index,HalfedgeId{12},EdgeId{22},LabelId{32}}},
        {{0,FaceId{40},LabelId{70}}});
    control::IfInstructionConfig config;
    scripting::ExpressionSpec expression;expression.program.source="area > 1";
    config.expression=expression;
    scripting::GeometryBindingPlan plan;plan.element_kind=scripting::BindingElementKind::face;
    plan.bindings={{"area",scripting::GeometryBindingKind::area}};config.geometry_bindings=plan;
    InstructionExecutionFrame request;request.inputs.node_id=13;
    request.inputs.promised_inputs={{"input",RuntimeValue::geometry(geometry,"bound",ActorId{"actor:bindings"})}};
    const auto result=control::make_if_instruction_handler(config)(request);
    const auto* selection=result.produced_outputs.empty()?nullptr:result.produced_outputs[0].value.as_element_selection();
    return !result.failure_message&&result.produced_outputs.size()==1
        &&result.produced_outputs[0].port=="then"&&selection
        &&selection->element_ids==std::vector<std::uint64_t>{40};
}

bool binding_configuration_changes_identity()
{
    using namespace phoenix;control::IfInstructionConfig first;
    scripting::ExpressionSpec expression;expression.program.source="length > 1";first.expression=expression;
    scripting::GeometryBindingPlan plan;plan.bindings={{"length",scripting::GeometryBindingKind::length,LabelId{30}}};first.geometry_bindings=plan;
    auto changed=first;changed.geometry_bindings->bindings[0].label1=LabelId{31};
    return control::configuration_revision(first)!=control::configuration_revision(changed);
}

bool routes_directed_halfedges()
{
    using namespace phoenix;
    auto geometry=CanonicalGeometry::create(
        {{{0,0,0},VertexId{1},0},{{2,0,0},VertexId{2},1},{{0,2,0},VertexId{3},2}},
        {{0,0,1,2,invalid_geometry_index,invalid_geometry_index,HalfedgeId{10},EdgeId{20},LabelId{30}},
         {1,0,2,0,invalid_geometry_index,invalid_geometry_index,HalfedgeId{11},EdgeId{21},LabelId{31}},
         {2,0,0,1,invalid_geometry_index,invalid_geometry_index,HalfedgeId{12},EdgeId{22},LabelId{32}}},
        {{0,FaceId{40},LabelId{70}}});
    control::IfInstructionConfig config;
    scripting::ExpressionSpec expression;expression.program.source="length > 2.5";config.expression=expression;
    scripting::GeometryBindingPlan plan;plan.element_kind=scripting::BindingElementKind::halfedge;
    plan.bindings={{"length",scripting::GeometryBindingKind::length}};config.geometry_bindings=plan;
    InstructionExecutionFrame request;request.inputs.node_id=14;
    request.inputs.promised_inputs={{"input",RuntimeValue::geometry(geometry,"bound",ActorId{"actor:halfedges"})}};
    const auto result=control::make_if_instruction_handler(config)(request);
    const ElementSelectionValue* selected=nullptr;
    for(const auto& output:result.produced_outputs)if(output.port=="then")selected=output.value.as_element_selection();
    return !result.failure_message&&selected&&selected->kind==GeometryElementKind::halfedge
        &&selected->element_ids==std::vector<std::uint64_t>{11};
}

} // namespace

int main()
{
    const bool routing = routes_supported_scalars();
    const bool errors = rejects_missing_and_unsupported();
    const bool identity = preserves_routed_value();
    const bool expression = routes_sandboxed_expression();
    const bool expression_contract = reports_expression_failure_and_stable_identity();
    const bool geometry_binding = routes_geometry_derived_binding();
    const bool binding_identity = binding_configuration_changes_identity();
    const bool halfedges = routes_directed_halfedges();
    std::cout << "if typed truthiness routing: " << routing << '\n'
              << "if invalid condition rejection: " << errors << '\n'
              << "if value preservation: " << identity << '\n'
              << "if sandboxed expression routing: " << expression << '\n'
              << "if expression diagnostics/identity: " << expression_contract << '\n'
              << "if geometry-derived binding: " << geometry_binding << '\n'
              << "if geometry-binding identity: " << binding_identity << '\n';
    std::cout << "if directed-halfedge routing: " << halfedges << '\n';
    return routing && errors && identity && expression && expression_contract && geometry_binding && binding_identity && halfedges ? 0 : 1;
}
