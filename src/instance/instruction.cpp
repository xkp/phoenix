#include "phoenix/instance/instruction.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <random>
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

double sample(const RangeStep& option,std::mt19937_64& engine)
{
    if(!option.range)return option.value;
    const auto low=std::min(option.value,*option.range),high=std::max(option.value,*option.range);
    if(option.step>0){const auto slots=static_cast<std::uint64_t>((high-low)/option.step);std::uniform_int_distribution<std::uint64_t> d(0,slots);return std::min(low+d(engine)*option.step,high);}
    std::uniform_real_distribution<double> d(low,high);return d(engine);
}

PlacementOptions sampled(const InstructionConfig& config,SeedValue seed)
{
    std::mt19937_64 engine(seed);auto result=config.placement;
    result.rotation_degrees={sample(config.ranges.rotation_x,engine),sample(config.ranges.rotation_y,engine),sample(config.ranges.rotation_z,engine)};
    result.scale={sample(config.ranges.scale_x,engine),sample(config.ranges.scale_y,engine),sample(config.ranges.scale_z,engine)};
    result.translation={sample(config.ranges.translation_x,engine),sample(config.ranges.translation_y,engine),sample(config.ranges.translation_z,engine)};
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
            const auto common_options=config.one_seed_each?std::optional<PlacementOptions>{}:std::optional<PlacementOptions>{sampled(config,base_seed)};
            for(GeometryIndex face=0;face<input.geometry->faces().size();++face){
                auto one=input.geometry->copy_face(face);auto options=common_options.value_or(sampled(config,SeedDeriver{}.derive({base_seed,frame.context.call_path,frame.inputs.node_id,std::nullopt,ordinal})));
                const auto planned=build_placements(*one,options);
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
    std::ostringstream s;s<<std::setprecision(17)<<"instance-v1|"<<c.prototype.stable_id()<<'|'<<static_cast<int>(c.placement.orientation)<<'|'<<static_cast<int>(c.placement.position)<<'|'<<(c.placement.orientation_label?c.placement.orientation_label->value():UINT64_MAX)<<'|'<<c.one_seed_each<<'|'<<c.remove_input;
    const RangeStep* values[]={&c.ranges.rotation_x,&c.ranges.rotation_y,&c.ranges.rotation_z,&c.ranges.scale_x,&c.ranges.scale_y,&c.ranges.scale_z,&c.ranges.translation_x,&c.ranges.translation_y,&c.ranges.translation_z};for(auto v:values)s<<'|'<<v->value<<'|'<<(v->range?*v->range:std::numeric_limits<double>::quiet_NaN())<<'|'<<v->step;return s.str();
}

} // namespace phoenix::instance
