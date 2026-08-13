#include "phoenix/merge/instruction.hpp"

#include <utility>

namespace phoenix::merge {
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
        if (!value) {
            result.failure_message = "Merge requires canonical geometry input.";
            return result;
        }
        if (!frame.element_ids) {
            result.failure_message = "Merge requires the run-scoped element ID allocator.";
            return result;
        }

        std::vector<GeometryValue> inputs;
        if (const auto* geometry = value->as_geometry()) {
            if (geometry->geometry) inputs.push_back(*geometry);
        } else if (const auto* collection = value->as_geometry_collection()) {
            for (const auto& geometry : collection->contributions)
                if (geometry.geometry) inputs.push_back(geometry);
        }
        if (inputs.empty()) {
            result.failure_message = "Merge requires at least one canonical geometry input.";
            return result;
        }

        ProductionMergeRequest request;
        request.options = config.options;
        for (const auto& input : inputs) {
            const auto input_owner = owner(frame, input);
            for (GeometryIndex face = 0; face < input.geometry->faces().size(); ++face) {
                request.sources.push_back({
                    {input.geometry, face, input.geometry->faces()[face].id}, input_owner});
            }
        }

        const auto merged = run_production_merge(request, *frame.element_ids);
        result.geometry_effects.push_back(merged.publication_effect(0));
        if (!merged.success()) {
            result.failures.push_back({frame.inputs.node_id, 0, merged.error,
                {{config.geometry_input_port, *value}}, frame.call_stack});
            result.produced_outputs.push_back(
                {config.geometry_output_port, RuntimeValue::geometry_collection({})});
            return result;
        }

        const auto output_owner = request.sources.front().owner_actor_id;
        result.produced_outputs.push_back({config.geometry_output_port,
            RuntimeValue::geometry(merged.geometry, "merge", output_owner)});
        return result;
    };
}

} // namespace phoenix::merge
