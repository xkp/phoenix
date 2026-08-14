#include "phoenix/control/choice_instruction.hpp"

#include <iostream>

namespace {
phoenix::InstructionExecutionFrame frame(phoenix::SeedValue seed=7){phoenix::InstructionExecutionFrame f;f.inputs.node_id=1;f.context.global_seed=seed;f.inputs.promised_inputs={{"input",phoenix::RuntimeValue::literal(std::int64_t{42})}};return f;}
bool explicit_and_random(){using namespace phoenix;control::ChoiceInstructionConfig c;c.items={{"a","A",{}},{"b","B",{}}};c.selected_output="b";const auto chosen=control::make_choice_instruction_handler(c)(frame());c.selected_output.reset();const auto first=control::make_choice_instruction_handler(c)(frame(91));const auto second=control::make_choice_instruction_handler(c)(frame(91));return chosen.produced_outputs.size()==1&&chosen.produced_outputs[0].port=="b"&&first.produced_outputs.size()==1&&second.produced_outputs.size()==1&&first.produced_outputs[0].port==second.produced_outputs[0].port;}
bool validates_and_tracks_identity(){using namespace phoenix;control::ChoiceInstructionConfig c;c.items={{"a",{},{}}};auto changed=c;changed.items[0].description="shown";auto invalid=c;invalid.selected_output="missing";const auto failed=control::make_choice_instruction_handler(invalid)(frame());return control::configuration_revision(c)!=control::configuration_revision(changed)&&failed.failure_message&&failed.produced_outputs.empty();}
}
int main(){const bool routing=explicit_and_random(),identity=validates_and_tracks_identity();std::cout<<"choice explicit/deterministic routing: "<<routing<<'\n'<<"choice validation/identity: "<<identity<<'\n';return routing&&identity?0:1;}
