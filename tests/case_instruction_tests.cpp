#include "phoenix/control/case_instruction.hpp"
#include "phoenix/scripting/quickjs_engine.hpp"

#include <iostream>

namespace {

phoenix::InstructionExecutionFrame frame(std::int64_t value)
{
    phoenix::InstructionExecutionFrame result;
    result.inputs.node_id = 12;
    result.context.global_seed = 91;
    result.inputs.promised_inputs = {
        {"input", phoenix::RuntimeValue::literal(phoenix::LiteralScalar{std::string{"payload"}})},
        {"value", phoenix::RuntimeValue::literal(phoenix::LiteralScalar{value})},
    };
    return result;
}

phoenix::control::CaseInstructionConfig config()
{
    using phoenix::control::CaseBranch;
    phoenix::control::CaseInstructionConfig result;
    phoenix::scripting::ExpressionSpec negative, even;
    negative.program.source = "value < 0";
    even.program.source = "value % 2 === 0";
    result.branches = {CaseBranch{"negative", negative}, CaseBranch{"even", even}};
    return result;
}

phoenix::CanonicalGeometryRef two_sized_triangles()
{
    using namespace phoenix;
    std::vector<RuntimeVertex> vertices{
        {{0,0,0},VertexId{1},0},{{2,0,0},VertexId{2},1},{{0,2,0},VertexId{3},2},
        {{4,0,0},VertexId{4},3},{{5,0,0},VertexId{5},4},{{4,1,0},VertexId{6},5}};
    std::vector<RuntimeHalfedge> edges{
        {0,0,1,2,invalid_geometry_index,invalid_geometry_index,HalfedgeId{10},EdgeId{20},LabelId{30}},
        {1,0,2,0,invalid_geometry_index,invalid_geometry_index,HalfedgeId{11},EdgeId{21},LabelId{31}},
        {2,0,0,1,invalid_geometry_index,invalid_geometry_index,HalfedgeId{12},EdgeId{22},LabelId{32}},
        {3,1,4,5,invalid_geometry_index,invalid_geometry_index,HalfedgeId{13},EdgeId{23},LabelId{30}},
        {4,1,5,3,invalid_geometry_index,invalid_geometry_index,HalfedgeId{14},EdgeId{24},LabelId{31}},
        {5,1,3,4,invalid_geometry_index,invalid_geometry_index,HalfedgeId{15},EdgeId{25},LabelId{32}}};
    return CanonicalGeometry::create(std::move(vertices),std::move(edges),
        {{0,FaceId{40},LabelId{70}},{3,FaceId{41},LabelId{71}}});
}

bool routes_geometry_derived_bindings()
{
    using namespace phoenix;
    auto configured=config();
    configured.branches[0].output_port="large";
    configured.branches[0].expression.program.source="area > 1";
    configured.branches.resize(1);
    scripting::GeometryBindingPlan plan;
    plan.element_kind=scripting::BindingElementKind::face;
    plan.bindings={{"area",scripting::GeometryBindingKind::area}};
    configured.geometry_bindings=plan;
    auto request=frame(0);
    request.inputs.promised_inputs[0].value=RuntimeValue::geometry(
        two_sized_triangles(),"bound",ActorId{"actor:bindings"});
    const auto result=control::make_case_instruction_handler(configured)(request);
    const ElementSelectionValue* large=nullptr;const ElementSelectionValue* fallback=nullptr;
    for(const auto& output:result.produced_outputs){if(output.port=="large")large=output.value.as_element_selection();if(output.port=="else")fallback=output.value.as_element_selection();}
    return !result.failure_message&&large&&fallback
        &&large->kind==GeometryElementKind::face&&large->element_ids==std::vector<std::uint64_t>{40}
        &&fallback->element_ids==std::vector<std::uint64_t>{41};
}

bool routes_each_geometry_collection_contribution()
{
    using namespace phoenix;
    auto configured=config();
    configured.branches={{"large",scripting::ExpressionSpec{}}};
    configured.branches[0].expression.program.source="area > 1";
    scripting::GeometryBindingPlan plan;plan.element_kind=scripting::BindingElementKind::face;
    plan.bindings={{"area",scripting::GeometryBindingKind::area}};configured.geometry_bindings=plan;
    const auto geometry=two_sized_triangles();
    GeometryCollectionValue collection{{{"first",ActorId{"actor:first"},geometry},{"second",ActorId{"actor:second"},geometry}}};
    auto request=frame(0);request.inputs.promised_inputs[0].value=RuntimeValue::geometry_collection(collection.contributions);
    const auto result=control::make_case_instruction_handler(configured)(request);
    std::size_t large=0,fallback=0;
    for(const auto& output:result.produced_outputs){if(output.port=="large")++large;if(output.port=="else")++fallback;}
    return !result.failure_message&&large==2&&fallback==2;
}

bool routes_first_truthy_and_else()
{
    const auto handler = phoenix::control::make_case_instruction_handler(config());
    const auto negative = handler(frame(-2));
    const auto even = handler(frame(4));
    const auto fallback = handler(frame(3));
    return negative.produced_outputs.size() == 1
        && negative.produced_outputs[0].port == "negative"
        && even.produced_outputs.size() == 1 && even.produced_outputs[0].port == "even"
        && fallback.produced_outputs.size() == 1
        && fallback.produced_outputs[0].port == "else";
}

bool preserves_value_and_rejects_errors()
{
    auto good = phoenix::control::make_case_instruction_handler(config())(frame(4));
    const auto* literal = good.produced_outputs[0].value.as_literal();
    const auto scalar = literal ? phoenix::literal_first_scalar(*literal) : std::nullopt;
    auto broken = config();
    broken.branches[0].expression.program.source = "value +";
    const auto failed = phoenix::control::make_case_instruction_handler(broken)(frame(1));
    return scalar && std::get<std::string>(*scalar) == "payload"
        && failed.produced_outputs.empty() && failed.failure_message.has_value();
}

bool identity_tracks_order_and_source()
{
    auto first = config();
    first.expression_engine = std::make_shared<phoenix::scripting::QuickJsEngine>();
    auto reordered = first;
    std::swap(reordered.branches[0], reordered.branches[1]);
    auto changed = first;
    changed.branches[0].expression.program.source = "value <= 0";
    return phoenix::control::configuration_revision(first)
            != phoenix::control::configuration_revision(reordered)
        && phoenix::control::configuration_revision(first)
            != phoenix::control::configuration_revision(changed);
}

} // namespace

int main()
{
    const bool routing = routes_first_truthy_and_else();
    const bool safety = preserves_value_and_rejects_errors();
    const bool identity = identity_tracks_order_and_source();
    const bool geometry_bindings = routes_geometry_derived_bindings();
    const bool collections = routes_each_geometry_collection_contribution();
    std::cout << "case ordered routing: " << routing << '\n'
              << "case value/error semantics: " << safety << '\n'
              << "case configuration identity: " << identity << '\n'
              << "case geometry-derived bindings: " << geometry_bindings << '\n';
    std::cout << "case geometry-collection bindings: " << collections << '\n';
    return routing && safety && identity && geometry_bindings && collections ? 0 : 1;
}
