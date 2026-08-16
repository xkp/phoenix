#include "phoenix/instance/instruction.hpp"

#include "phoenix/scripting/quickjs_engine.hpp"

#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace phoenix::instance {
namespace {

const RuntimeValue* find_input(const InstructionExecutionFrame& frame,const PortId& port)
{ for(const auto& input:frame.inputs.promised_inputs)if(input.port==port)return &input.value;return nullptr; }

void append_inputs(std::vector<GeometryValue>& inputs,const RuntimeValue& value)
{
    if(const auto* geometry=value.as_geometry()){if(geometry->geometry)inputs.push_back(*geometry);}
    else if(const auto* collection=value.as_geometry_collection())for(const auto& geometry:collection->contributions)if(geometry.geometry)inputs.push_back(geometry);
}

std::optional<double> sample(
    const RangeStep& option,
    const InstructionExecutionFrame& frame,
    std::string& error)
{
    const auto evaluated = scripting::evaluate_numeric_range(option.value, frame);
    if (evaluated.error) {
        error = *evaluated.error;
        return std::nullopt;
    }
    return evaluated.value;
}

std::optional<PlacementOptions> sampled(
    const InstructionConfig& config,
    const InstructionExecutionFrame& frame,
    std::string& error)
{
    auto result=config.placement;
    const auto rx=sample(config.ranges.rotation_x,frame,error);
    const auto ry=sample(config.ranges.rotation_y,frame,error);
    const auto rz=sample(config.ranges.rotation_z,frame,error);
    const auto sx=sample(config.ranges.scale_x,frame,error);
    const auto sy=sample(config.ranges.scale_y,frame,error);
    const auto sz=sample(config.ranges.scale_z,frame,error);
    const auto tx=sample(config.ranges.translation_x,frame,error);
    const auto ty=sample(config.ranges.translation_y,frame,error);
    const auto tz=sample(config.ranges.translation_z,frame,error);
    if(!rx||!ry||!rz||!sx||!sy||!sz||!tx||!ty||!tz)return std::nullopt;
    result.rotation_degrees={*rx,*ry,*rz};
    result.scale={*sx,*sy,*sz};
    result.translation={*tx,*ty,*tz};
    return result;
}

ActorId placement_id(const InstructionExecutionFrame& frame,const FaceId& face,std::size_t ordinal)
{
    std::ostringstream s;s<<"instance";for(const auto& part:frame.context.call_path)s<<':'<<part;s<<':'<<frame.inputs.node_id<<':'<<face.value()<<':'<<ordinal;return s.str();
}

} // namespace

ActorId ExternalPrototypeIdentity::stable_id() const
{
    std::ostringstream s;s<<"external-prototype:"<<asset_uid.size()<<':'<<asset_uid<<':'<<asset_version<<':'<<std::hex<<content_fingerprint;return s.str();
}

InstructionHandler make_instruction_handler(InstructionConfig config)
{
    return [config=std::move(config)](const InstructionExecutionFrame& frame){
        InstructionResult result;result.node_id=frame.inputs.node_id;
        const auto* value=find_input(frame,config.geometry_input_port);
        if(!value){result.failure_message="Instance requires canonical geometry input.";return result;}
        if(!config.prototype.valid()){result.failure_message="Instance requires an immutable external prototype identity.";return result;}
        std::vector<GeometryValue> inputs;append_inputs(inputs,*value);
        if(inputs.empty()){result.failure_message="Instance requires canonical geometry input.";return result;}
        const auto base_seed=frame.effective_seed.value_or(SeedDeriver{}.derive(frame.seed_derivation));
        std::vector<ActorNode> children;std::vector<GeometryItemEffect> effects;std::size_t ordinal=0;
        for(const auto& input:inputs){
            std::string sample_error;
            auto common_frame=frame;
            common_frame.effective_seed=base_seed;
            const auto common_options=config.one_seed_each
                ? std::optional<PlacementOptions>{}
                : sampled(config,common_frame,sample_error);
            if(!sample_error.empty()){result.failure_message=sample_error;return result;}
            for(GeometryIndex face=0;face<input.geometry->faces().size();++face){
                auto one=input.geometry->copy_face(face);
                auto options=common_options;
                if(!options){
                    auto item_frame=frame;
                    item_frame.effective_seed=SeedDeriver{}.derive({base_seed,frame.context.call_path,frame.inputs.node_id,std::nullopt,ordinal});
                    options=sampled(config,item_frame,sample_error);
                    if(!options){result.failure_message=sample_error;return result;}
                }
                const auto planned=build_placements(*one,*options);
                if(!planned.success()){
                    const auto message=planned.diagnostics.empty()?"Instance placement failed.":planned.diagnostics.front();
                    result.failures.push_back({frame.inputs.node_id,ordinal,message,{{config.geometry_input_port,*value}},frame.call_stack});
                    result.geometry_effects.push_back({ordinal,false,nullptr,{},message});
                    result.produced_outputs.push_back({config.output_port,RuntimeValue::empty()});return result;
                }
                const auto& placement=planned.placements.front();ActorNode child;child.id=placement_id(frame,input.geometry->faces()[face].id,ordinal);child.name=config.prototype.display_name;
                child.prototype=ActorPrototypeRef{config.prototype.stable_id()};child.transform.translation={placement.origin.x,placement.origin.y,placement.origin.z};const auto euler=quaternion_to_euler_xyz(placement.rotation);child.transform.rotation_euler={euler.x,euler.y,euler.z};child.transform.scale={placement.scale.x,placement.scale.y,placement.scale.z};children.push_back(std::move(child));
                GeometryItemEffect effect;effect.item_key=ordinal;if(config.remove_input){const auto owner=input.accumulation_actor_id.value_or("actor:root");effect.consumed_faces.push_back({owner,input.geometry->faces()[face].id});effect.allows_empty_replacement=true;}effects.push_back(std::move(effect));++ordinal;
            }
        }
        result.actor_children=std::move(children);result.geometry_effects=std::move(effects);result.produced_outputs.push_back({config.output_port,RuntimeValue::empty()});return result;
    };
}

std::string configuration_revision(const InstructionConfig& c)
{
    const auto& engine = scripting::QuickJsEngine{};
    std::ostringstream s;s<<std::setprecision(17)<<"instance-v1|"<<c.prototype.stable_id()<<'|'<<static_cast<int>(c.placement.orientation)<<'|'<<static_cast<int>(c.placement.position)<<'|'<<(c.placement.orientation_label?c.placement.orientation_label->value():UINT64_MAX)<<'|'<<c.one_seed_each<<'|'<<c.remove_input;
    const RangeStep* values[]={&c.ranges.rotation_x,&c.ranges.rotation_y,&c.ranges.rotation_z,&c.ranges.scale_x,&c.ranges.scale_y,&c.ranges.scale_z,&c.ranges.translation_x,&c.ranges.translation_y,&c.ranges.translation_z};for(auto v:values)s<<'|'<<scripting::numeric_range_revision(v->value,engine);return s.str();
}

} // namespace phoenix::instance
