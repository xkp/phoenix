#include "phoenix/control/choice_instruction.hpp"

#include <random>
#include <set>
#include <sstream>

namespace phoenix::control {

std::string configuration_revision(const ChoiceInstructionConfig& config)
{
    std::ostringstream stream;
    stream << "choice-v1|" << config.input_port << '|' << config.published_name
           << '|' << config.published_group << '|' << config.always_visible
           << '|' << config.important << '|'
           << (config.selected_output ? *config.selected_output : std::string{});
    for (const auto& item : config.items)
        stream << '|' << item.output_port << ':' << item.description << ':' << item.image;
    return stream.str();
}

InstructionHandler make_choice_instruction_handler(ChoiceInstructionConfig config)
{
    return [config=std::move(config)](const InstructionExecutionFrame& frame) {
        InstructionResult result;result.node_id=frame.inputs.node_id;
        const RuntimeValue* input=nullptr;
        for(const auto& candidate:frame.inputs.promised_inputs)
            if(candidate.port==config.input_port){input=&candidate.value;break;}
        if(!input||input->is_missing()||input->is_empty())return result;
        if(config.items.empty()){result.failure_message="Choice requires at least one item.";return result;}
        std::set<PortId> ports;
        for(const auto& item:config.items)
            if(item.output_port.empty()||!ports.insert(item.output_port).second){result.failure_message="Choice output ports must be non-empty and unique.";return result;}
        const ChoiceItem* selected=nullptr;
        if(config.selected_output){
            for(const auto& item:config.items)if(item.output_port==*config.selected_output){selected=&item;break;}
            if(!selected){result.failure_message="Configured choice output does not exist: "+*config.selected_output;return result;}
        } else {
            std::mt19937_64 engine(frame.effective_seed.value_or(frame.context.global_seed));
            std::uniform_int_distribution<std::size_t> distribution(0,config.items.size()-1);
            selected=&config.items[distribution(engine)];
        }
        result.produced_outputs.push_back({selected->output_port,*input});
        return result;
    };
}

} // namespace phoenix::control
