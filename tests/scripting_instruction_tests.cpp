#include "phoenix/scripting/instruction.hpp"

#include <iostream>

namespace {

phoenix::CanonicalGeometryRef triangle()
{
    using namespace phoenix;
    return CanonicalGeometry::create(
        {{{0,0,0},VertexId{1},0},{{1,0,0},VertexId{2},1},{{0,1,0},VertexId{3},2}},
        {{0,0,1,2,invalid_geometry_index,invalid_geometry_index,HalfedgeId{10},EdgeId{20},LabelId{31}},
         {1,0,2,0,invalid_geometry_index,invalid_geometry_index,HalfedgeId{11},EdgeId{21},LabelId{32}},
         {2,0,0,1,invalid_geometry_index,invalid_geometry_index,HalfedgeId{12},EdgeId{22},LabelId{33}}},
        {{0,FaceId{40},LabelId{70}}});
}

phoenix::FunctionDescriptor function()
{
    using namespace phoenix;
    FunctionDescriptor result;result.id="script-integration";
    result.input_ports={{"geometry","geometry",PortDirection::input},{"height","number",PortDirection::input}};
    result.output_ports={{"mesh","geometry",PortDirection::output},{"twice","number",PortDirection::output},
        {"faces","faces",PortDirection::output}};
    InstructionDescriptor script;script.id=1;script.kind="script";
    script.input_ports=result.input_ports;
    script.output_ports=result.output_ports;
    InstructionDescriptor output;output.id=99;output.kind="output";output.input_ports={
        {"mesh","geometry",PortDirection::input},{"twice","number",PortDirection::input},
        {"faces","faces",PortDirection::input}};
    result.instructions={script,output};result.edges={{1,"mesh",99,"mesh"},{1,"twice",99,"twice"},
        {1,"faces",99,"faces"}};
    result.output_node_id=99;return result;
}

bool executes_as_graph_instruction()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    InstructionConfig config;
    config.program.source=
        "const mesh=td.cloneGeometry('geometry','mesh');"
        "td.setVertex(mesh,0,0,td.vars.height,0);const faces=td.elements(mesh,'faces');"
        "return {mesh,twice:td.vars.height*2,faces};";
    config.outputs={{"mesh",ScriptOutputKind::geometry},{"twice",ScriptOutputKind::scalar},
        {"faces",ScriptOutputKind::faces}};
    InstructionRegistry registry;registry.register_handler("script",make_instruction_handler(config));
    const auto descriptor=function();RunElementIdAllocator ids{2000};
    FunctionExecutionRequest request;request.function=&descriptor;
    request.inputs={{"geometry",RuntimeValue::geometry(triangle(),"source",ActorId{"actor"})},
        {"height",RuntimeValue::literal(LiteralValue{LiteralScalar{3.5}})}};
    request.context={descriptor.id,{"script"},42};request.element_ids=&ids;
    const auto result=FunctionExecutor{registry}.run(request);
    if(result.status!=FunctionExecutionStatus::completed||result.outputs.size()!=3)return false;
    const auto* geometry=result.outputs[0].value.as_geometry();
    const auto scalar=result.outputs[1].value.as_literal();
    const auto* selection=result.outputs[2].value.as_element_selection();
    const auto value=scalar?literal_first_scalar(*scalar):std::optional<LiteralScalar>{};
    return geometry&&geometry->geometry&&geometry->geometry->vertices()[0].point.y==3.5&&
        value&&std::get<double>(*value)==7.0&&selection&&
        selection->kind==GeometryElementKind::face&&
        selection->element_ids==std::vector<std::uint64_t>{40}&&selection->source.geometry&&
        selection->source.geometry->vertices()[0].point.y==3.5;
}

bool forwards_selection_into_downstream_script()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    FunctionDescriptor descriptor;descriptor.id="script-selection-chain";
    descriptor.input_ports={{"geometry","geometry",PortDirection::input}};
    descriptor.output_ports={{"faces","faces",PortDirection::output}};
    InstructionDescriptor select;select.id=1;select.kind="script-select";
    select.input_ports=descriptor.input_ports;select.output_ports={{"faces","faces",PortDirection::output}};
    InstructionDescriptor pass;pass.id=2;pass.kind="script-pass";
    pass.input_ports={{"selected","faces",PortDirection::input}};
    pass.output_ports={{"faces","faces",PortDirection::output}};
    InstructionDescriptor output;output.id=99;output.kind="output";
    output.input_ports={{"faces","faces",PortDirection::input}};
    descriptor.instructions={select,pass,output};descriptor.edges={
        {1,"faces",2,"selected"},{2,"faces",99,"faces"}};descriptor.output_node_id=99;
    InstructionConfig select_config;select_config.program.source=
        "const mesh=td.cloneGeometry('geometry','privateMesh');"
        "return {faces:td.elements(mesh,'faces')};";
    select_config.outputs={{"privateMesh",ScriptOutputKind::geometry},{"faces",ScriptOutputKind::faces}};
    InstructionConfig pass_config;pass_config.program.source=
        "return {faces:td.inputs.selected.faces3};";
    pass_config.outputs={{"faces",ScriptOutputKind::faces}};
    InstructionRegistry registry;
    registry.register_handler("script-select",make_instruction_handler(select_config));
    registry.register_handler("script-pass",make_instruction_handler(pass_config));
    RunElementIdAllocator ids{3000};FunctionExecutionRequest request;request.function=&descriptor;
    request.inputs={{"geometry",RuntimeValue::geometry(triangle(),"source",ActorId{"actor"})}};
    request.context={descriptor.id,{"selection"},7};request.element_ids=&ids;
    const auto result=FunctionExecutor{registry}.run(request);
    const auto* selection=result.outputs.empty()?nullptr:result.outputs[0].value.as_element_selection();
    return result.status==FunctionExecutionStatus::completed&&selection&&
        selection->kind==GeometryElementKind::face&&
        selection->element_ids==std::vector<std::uint64_t>{40};
}

} // namespace

int main()
{
    const bool graph=executes_as_graph_instruction();
    const bool selection=forwards_selection_into_downstream_script();
    std::cout<<"script graph instruction end-to-end: "<<graph<<'\n'
             <<"script selection input round trip: "<<selection<<'\n';
    return graph&&selection?0:1;
}
