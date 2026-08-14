#include "phoenix/scripting/instruction.hpp"

#include "phoenix/scripting/quickjs_engine.hpp"

#include <sstream>
#include <stdexcept>

namespace phoenix::scripting {
namespace {

ActorId owner(const GeometryValue& geometry)
{
    return geometry.accumulation_actor_id.value_or(ActorId{"root"});
}

std::optional<Value> scalar(const RuntimeValue& value)
{
    const auto* literal=value.as_literal();
    if(!literal)return {};
    const auto first=literal_first_scalar(*literal);
    if(!first)return {};
    return std::visit([](const auto& item)->Value{return item;},*first);
}

std::string diagnostics(const ScriptResult& result)
{
    std::ostringstream stream;
    for(std::size_t i=0;i<result.diagnostics.size();++i){
        if(i)stream<<"; ";
        stream<<to_string(result.diagnostics[i].code)<<": "<<result.diagnostics[i].message;
    }
    return stream.str().empty()?"Script execution failed.":stream.str();
}

} // namespace

InstructionHandler make_instruction_handler(InstructionConfig config)
{
    if(!config.engine)config.engine=std::make_shared<QuickJsEngine>();
    return [config=std::move(config)](const InstructionExecutionFrame& frame){
        InstructionResult result;result.node_id=frame.inputs.node_id;
        if(!frame.element_ids){result.failure_message="Script instruction requires the run element-ID allocator.";return result;}
        ScriptRequest request;request.program=config.program;request.bindings=config.bindings;
        request.labels=config.labels;request.libraries=config.libraries;request.limits=config.limits;
        request.deterministic_seed=frame.effective_seed.value_or(frame.context.global_seed);
        std::optional<ActorId> publication_owner;
        if(const auto* current=frame.call_stack.current();current&&current->actor_id)publication_owner=current->actor_id;
        for(const auto& input:frame.inputs.promised_inputs){
            if(const auto* geometry=input.value.as_geometry()){
                if(geometry->geometry){const auto input_owner=owner(*geometry);request.geometry_inputs.push_back({input.port,geometry->geometry,input_owner});if(!publication_owner)publication_owner=input_owner;}
            }else if(const auto* collection=input.value.as_geometry_collection()){
                if(collection->contributions.size()!=1){result.failure_message="Script geometry collection inputs must contain exactly one contribution per port.";return result;}
                const auto& geometry=collection->contributions.front();
                if(geometry.geometry){const auto input_owner=owner(geometry);request.geometry_inputs.push_back({input.port,geometry.geometry,input_owner});if(!publication_owner)publication_owner=input_owner;}
            }else if(const auto* selection=input.value.as_element_selection()){
                const auto kind=selection->kind==GeometryElementKind::vertex?ScriptOutputKind::vertices:
                    selection->kind==GeometryElementKind::halfedge?ScriptOutputKind::halfedges:
                    ScriptOutputKind::faces;
                request.geometry_inputs.push_back({input.port,selection->source.geometry,
                    owner(selection->source),kind,selection->element_ids});
                if(!publication_owner)publication_owner=owner(selection->source);
            }else if(const auto value=scalar(input.value))request.bindings[input.port]=*value;
            else {result.failure_message="Unsupported script input value on port '"+input.port+"'.";return result;}
        }
        try{
            const auto session=(static_cast<std::uint64_t>(frame.inputs.node_id)<<32U)^request.deterministic_seed;
            InvocationGeometryHost host{session,request,config.outputs,frame.element_ids};
            const auto script=config.engine->execute_script(request,host);
            const auto committed=host.finalize(script);
            if(!committed.success()){result.failure_message=diagnostics(script);if(script.success()){result.failure_message=committed.diagnostics.front().message;}return result;}
            for(const auto& output:committed.outputs){
                if(output.kind==ScriptOutputKind::scalar){
                    const auto literal=std::visit([](const auto& item)->LiteralScalar{return item;},*output.scalar);
                    result.produced_outputs.push_back({output.port,RuntimeValue::literal(LiteralValue{literal})});
                }else if(output.kind==ScriptOutputKind::geometry){
                    result.produced_outputs.push_back({output.port,RuntimeValue::geometry(output.geometry,"script",publication_owner.value_or(ActorId{"root"}))});
                }else{
                    const auto kind=output.kind==ScriptOutputKind::vertices?GeometryElementKind::vertex:
                        output.kind==ScriptOutputKind::halfedges?GeometryElementKind::halfedge:
                        GeometryElementKind::face;
                    ElementSelectionValue selection{{"script-selection",
                        publication_owner.value_or(ActorId{"root"}),output.geometry},
                        kind,output.element_ids};
                    result.produced_outputs.push_back({output.port,
                        RuntimeValue::element_selection(std::move(selection))});
                }
            }
        }catch(const std::exception& error){result.failure_message=error.what();}
        return result;
    };
}

} // namespace phoenix::scripting
