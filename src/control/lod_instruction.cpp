#include "phoenix/control/lod_instruction.hpp"

#include <sstream>

namespace phoenix::control {

std::string configuration_revision(const LodInstructionConfig& config)
{
    std::ostringstream stream;stream<<"lod-v1|"<<config.input_port<<'|'<<config.low_port<<'|'<<config.normal_port<<'|'<<config.high_port;
    for(const auto level:config.connected_levels)stream<<'|'<<static_cast<int>(level);
    return stream.str();
}

InstructionHandler make_lod_instruction_handler(LodInstructionConfig config)
{
    return [config=std::move(config)](const InstructionExecutionFrame& frame) {
        InstructionResult result;result.node_id=frame.inputs.node_id;
        const RuntimeValue* input=nullptr;for(const auto& candidate:frame.inputs.promised_inputs)if(candidate.port==config.input_port){input=&candidate.value;break;}
        if(!input||input->is_missing()||input->is_empty())return result;
        auto requested=frame.context.lod;if(requested>2)requested=2;
        for(;requested>=0;--requested){const auto level=static_cast<LodLevel>(requested);if(config.connected_levels.find(level)==config.connected_levels.end())continue;
            const auto& port=level==LodLevel::high?config.high_port:level==LodLevel::normal?config.normal_port:config.low_port;
            result.produced_outputs.push_back({port,*input});return result;}
        return result;
    };
}

} // namespace phoenix::control
