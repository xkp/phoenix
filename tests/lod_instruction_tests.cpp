#include "phoenix/control/lod_instruction.hpp"

#include <iostream>

namespace {
phoenix::InstructionExecutionFrame frame(std::int32_t lod){phoenix::InstructionExecutionFrame f;f.inputs.node_id=2;f.context.lod=lod;f.inputs.promised_inputs={{"input",phoenix::RuntimeValue::literal(std::int64_t{9})}};return f;}
bool fallback(){using namespace phoenix;control::LodInstructionConfig c;c.connected_levels={control::LodLevel::low,control::LodLevel::high};const auto high=control::make_lod_instruction_handler(c)(frame(2));const auto normal=control::make_lod_instruction_handler(c)(frame(1));const auto below=control::make_lod_instruction_handler(c)(frame(-1));return high.produced_outputs.size()==1&&high.produced_outputs[0].port=="high"&&normal.produced_outputs.size()==1&&normal.produced_outputs[0].port=="low"&&below.produced_outputs.empty();}
bool identity(){phoenix::control::LodInstructionConfig a;auto b=a;b.connected_levels.erase(phoenix::control::LodLevel::high);return phoenix::control::configuration_revision(a)!=phoenix::control::configuration_revision(b);}
}
int main(){const bool routing=fallback(),revision=identity();std::cout<<"lod lower-connected fallback: "<<routing<<'\n'<<"lod configuration identity: "<<revision<<'\n';return routing&&revision?0:1;}
