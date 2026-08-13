#include "phoenix/partition/instruction.hpp"

#include <cstdint>
#include <utility>

namespace phoenix::partition {
namespace {

const RuntimeValue* find_input(const InstructionExecutionFrame& frame, const PortId& port)
{
    for (const auto& input : frame.inputs.promised_inputs)
        if (input.port == port) return &input.value;
    return nullptr;
}

ActorId geometry_owner(const InstructionExecutionFrame& frame, const GeometryValue& geometry)
{
    if (geometry.accumulation_actor_id) return *geometry.accumulation_actor_id;
    const auto* call = frame.call_stack.current();
    return call && call->actor_id ? *call->actor_id : ActorId{"root"};
}

InstructionFailure item_failure(
    const InstructionExecutionFrame& frame,
    const InstructionConfig& config,
    const GeometryValue& source,
    CanonicalGeometryRef face,
    std::uint64_t item,
    std::string message)
{
    return InstructionFailure{
        frame.inputs.node_id, item, std::move(message),
        {{config.geometry_input_port, RuntimeValue::geometry(
            std::move(face), source.debug_label, source.accumulation_actor_id)}},
        frame.call_stack};
}

} // namespace

InstructionHandler make_instruction_handler(InstructionConfig config)
{
    return [config = std::move(config)](const InstructionExecutionFrame& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        const auto* value = find_input(frame, config.geometry_input_port);
        const auto* geometry = value ? value->as_geometry() : nullptr;
        if (!geometry || !geometry->geometry) {
            result.failure_message = "Partition requires one canonical geometry input.";
            return result;
        }
        if (!config.model_factory) {
            result.failure_message = "Partition requires a linked production model factory.";
            return result;
        }
        if (!frame.element_ids) {
            result.failure_message = "Partition requires the run-scoped element ID allocator.";
            return result;
        }

        const auto owner = geometry_owner(frame, *geometry);
        std::vector<GeometryValue> outputs;
        for (GeometryIndex face_index = 0;
             face_index < geometry->geometry->faces().size(); ++face_index) {
            const auto source_face = geometry->geometry->copy_face(face_index);
            GeometryItemEffect effect;
            effect.item_key = face_index;
            const auto model = config.model_factory();
            if (!model) {
                effect.succeeded = false;
                effect.failure_message = "Partition model factory returned no model.";
            } else {
                ProductionPartitionRequest request;
                request.source = {geometry->geometry, face_index,
                    geometry->geometry->faces()[face_index].id};
                request.model = model.get();
                request.values = config.values;
                request.base_segment_labels = config.base_segment_labels;
                request.random_seed = static_cast<std::int32_t>(
                    frame.derive_item_seed(face_index));
                const auto partitioned = run_production_partition(
                    request, *frame.element_ids);
                effect = partitioned.publication_effect(face_index, owner);
                if (partitioned.success()) {
                    outputs.push_back(GeometryValue{
                        "partition", owner, partitioned.geometry});
                }
            }
            if (!effect.succeeded) {
                result.failures.push_back(item_failure(frame, config, *geometry,
                    source_face, effect.item_key, effect.failure_message));
            }
            result.geometry_effects.push_back(std::move(effect));
        }
        result.produced_outputs.push_back({config.geometry_output_port,
            RuntimeValue::geometry_collection(std::move(outputs))});
        return result;
    };
}

} // namespace phoenix::partition
