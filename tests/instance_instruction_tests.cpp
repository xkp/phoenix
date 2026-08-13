#include "phoenix/instance/instruction.hpp"

#include <cmath>
#include <iostream>

namespace {

phoenix::CanonicalGeometryRef quad()
{
    using namespace phoenix;
    return CanonicalGeometry::create({{{0,0,0},VertexId{1},0},{{2,0,0},VertexId{2},1},{{2,2,0},VertexId{3},2},{{0,2,0},VertexId{4},3}},
        {{0,0,1,3,invalid_geometry_index,0,HalfedgeId{10},EdgeId{20},LabelId{31}},
         {1,0,2,0,invalid_geometry_index,1,HalfedgeId{11},EdgeId{21},LabelId{32}},
         {2,0,3,1,invalid_geometry_index,2,HalfedgeId{12},EdgeId{22},LabelId{33}},
         {3,0,0,2,invalid_geometry_index,3,HalfedgeId{13},EdgeId{23},LabelId{34}}},{{0,FaceId{40},LabelId{70}}});
}

phoenix::FunctionDescriptor function(const std::string& revision,bool consumes)
{
    using namespace phoenix;FunctionDescriptor f;f.id="instance-e2e";f.input_ports={{"input","geometry",PortDirection::input}};
    InstructionDescriptor i;i.id=1;i.kind="instance";i.input_ports=f.input_ports;i.consumes_geometry=consumes;i.configuration_revision=revision;f.instructions={i};return f;
}

phoenix::instance::InstructionConfig config(bool remove=false)
{
    phoenix::instance::InstructionConfig c;c.prototype={"asset:tree",3,0xabc123,std::string{"Tree"}};c.placement.orientation_label=phoenix::LabelId{31};c.remove_input=remove;c.ranges.translation_x={1.0,3.0,1.0};return c;
}

bool actor_placement_cache_and_identity()
{
    using namespace phoenix;auto c=config();const auto f=function(instance::configuration_revision(c),false);std::size_t runs=0;
    const auto handler=instance::make_instruction_handler(c);InstructionRegistry registry;registry.register_handler("instance",[&](const auto& frame){++runs;return handler(frame);});
    GeometryPublicationLedger ledger;MemoryCacheStore cache;FunctionExecutionRequest request;request.function=&f;request.inputs={{"input",RuntimeValue::geometry(quad(),"source",ActorId{"actor:instance"})}};request.context={f.id,{"instance"},55};request.publication_ledger=&ledger;request.cache_store=&cache;request.cache_writer=&cache;request.kernel_version="instance-placement-v1";
    const FunctionExecutor executor{registry};const auto first=executor.run(request);const auto second=executor.run(request);
    if(!first.actor||first.actor->children.size()!=1||!second.actor||second.actor->children.size()!=1)return false;
    const auto& child=first.actor->children[0];return runs==1&&child.prototype&&child.prototype->prototype_id==c.prototype.stable_id()&&child.name==std::optional<std::string>{"Tree"}&&child.id==second.actor->children[0].id&&!ledger.is_consumed({"actor:instance",FaceId{40}});
}

bool remove_input_is_intentional_empty_replacement()
{
    using namespace phoenix;auto c=config(true);const auto f=function(instance::configuration_revision(c),true);InstructionRegistry registry;registry.register_handler("instance",instance::make_instruction_handler(c));GeometryPublicationLedger ledger;FunctionExecutionRequest request;request.function=&f;request.inputs={{"input",RuntimeValue::geometry(quad(),"source",ActorId{"actor:remove"})}};request.context={f.id,{"remove"},1};request.publication_ledger=&ledger;const auto result=FunctionExecutor{registry}.run(request);const auto remaining=ledger.assemble_actor("actor:remove");return result.status==FunctionExecutionStatus::completed&&result.actor&&result.actor->children.size()==1&&ledger.is_consumed({"actor:remove",FaceId{40}})&&remaining&&remaining->faces().empty();
}

bool deterministic_ranges_and_failure()
{
    using namespace phoenix;auto c=config();c.one_seed_each=true;const auto handler=instance::make_instruction_handler(c);InstructionExecutionFrame frame;frame.inputs.node_id=7;frame.context={"f",{"same"},99};frame.seed_derivation={99,{"same"},7,std::nullopt,std::nullopt};frame.effective_seed=SeedDeriver{}.derive(frame.seed_derivation);frame.inputs.promised_inputs={{"input",RuntimeValue::geometry(quad(),"source",ActorId{"actor:ranges"})}};const auto a=handler(frame),b=handler(frame);if(a.actor_children.size()!=1||b.actor_children.size()!=1)return false;
    c.placement.orientation_label=LabelId{999};const auto failed=instance::make_instruction_handler(c)(frame);
    return std::abs(a.actor_children[0].transform.translation.x-b.actor_children[0].transform.translation.x)<1e-12&&failed.actor_children.empty()&&failed.failures.size()==1&&!failed.geometry_effects[0].succeeded&&failed.geometry_effects[0].consumed_faces.empty();
}

}

int main(){const bool a=actor_placement_cache_and_identity(),b=remove_input_is_intentional_empty_replacement(),c=deterministic_ranges_and_failure();std::cout<<"instance actor placement and cache: "<<a<<'\n'<<"instance optional transactional consumption: "<<b<<'\n'<<"instance deterministic ranges and failure: "<<c<<'\n';return a&&b&&c?0:1;}
