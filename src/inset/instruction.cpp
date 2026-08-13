#include "phoenix/inset/instruction.hpp"

#include <utility>

namespace phoenix::inset {
namespace {

const RuntimeValue* find_input(const InstructionExecutionFrame& frame, const PortId& port)
{
    for (const auto& input : frame.inputs.promised_inputs)
        if (input.port == port) return &input.value;
    return nullptr;
}

ActorId owner(const InstructionExecutionFrame& frame, const GeometryValue& geometry)
{
    if (geometry.accumulation_actor_id) return *geometry.accumulation_actor_id;
    const auto* call = frame.call_stack.current();
    return call && call->actor_id ? *call->actor_id : ActorId{"root"};
}

} // namespace

InstructionHandler make_instruction_handler(InstructionConfig config)
{
    return [config = std::move(config)](const InstructionExecutionFrame& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        const auto* value = find_input(frame, config.geometry_input_port);
        const auto* input = value ? value->as_geometry() : nullptr;
        if (!input || !input->geometry) {
            result.failure_message = "Inset requires one canonical geometry input.";
            return result;
        }
        if (!frame.element_ids) {
            result.failure_message = "Inset requires the run-scoped element ID allocator.";
            return result;
        }
        const auto actor = owner(frame, *input);
        std::vector<GeometryValue> outputs;
        for (GeometryIndex face = 0; face < input->geometry->faces().size(); ++face) {
            ProductionInsetRequest request;
            request.source = {input->geometry, face, input->geometry->faces()[face].id};
            request.amount = config.amount;
            request.labels = config.labels;
            const auto inset = run_production_inset(request, *frame.element_ids);
            auto effect = inset.publication_effect(face, actor);
            if (inset.success()) {
                outputs.push_back({"inset", actor, inset.geometry});
            } else {
                result.failures.push_back({frame.inputs.node_id, face, inset.error,
                    {{config.geometry_input_port, RuntimeValue::geometry(
                        input->geometry->copy_face(face), input->debug_label,
                        input->accumulation_actor_id)}}, frame.call_stack});
            }
            result.geometry_effects.push_back(std::move(effect));
        }
        result.produced_outputs.push_back({config.geometry_output_port,
            RuntimeValue::geometry_collection(std::move(outputs))});
        return result;
    };
}

} // namespace phoenix::inset
