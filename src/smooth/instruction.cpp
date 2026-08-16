#include "phoenix/smooth/instruction.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace phoenix::smooth {
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

GeometryItemEffect failure_effect(std::uint64_t item, std::string message)
{
    GeometryItemEffect effect;
    effect.item_key = item;
    effect.succeeded = false;
    effect.failure_message = std::move(message);
    return effect;
}

} // namespace

InstructionHandler make_instruction_handler(InstructionConfig config)
{
    return [config = std::move(config)](const InstructionExecutionFrame& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        if (config.unsupported_runtime_reason) {
            result.failure_message = *config.unsupported_runtime_reason;
            return result;
        }

        const auto* value = find_input(frame, config.geometry_input_port);
        if (!value) {
            result.failure_message = "Smooth requires canonical geometry input.";
            return result;
        }
        if (!frame.element_ids) {
            result.failure_message = "Smooth requires the run-scoped element ID allocator.";
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
            result.failure_message = "Smooth requires at least one canonical geometry input.";
            return result;
        }

        auto evaluated_level = scripting::evaluate_numeric(config.max_refinement_level, frame);
        if (evaluated_level.error) {
            result.failure_message = *evaluated_level.error;
            return result;
        }
        auto options = config.options;
        options.max_refinement_level = static_cast<std::uint32_t>(
            std::clamp(std::llround(evaluated_level.value), 0LL, 16LL));

        std::vector<GeometryValue> outputs;
        outputs.reserve(inputs.size());
        for (std::size_t item = 0; item < inputs.size(); ++item) {
            const auto& input = inputs[item];
            const auto input_owner = owner(frame, input);
            const auto refined = subdivide(*input.geometry, *frame.element_ids, options);
            if (!refined.success()) {
                const std::string message = refined.diagnostics.empty()
                    ? "OpenSubdiv refinement failed." : refined.diagnostics.front().message;
                result.geometry_effects.push_back(failure_effect(item, message));
                result.failures.push_back({frame.inputs.node_id, item, message,
                    {{config.geometry_input_port, RuntimeValue::geometry(
                        input.geometry, input.debug_label, input.accumulation_actor_id)}},
                    frame.call_stack});
                continue;
            }

            GeometryItemEffect effect;
            effect.item_key = item;
            effect.generated_geometry = refined.geometry;
            for (const auto& face : input.geometry->faces())
                effect.consumed_faces.push_back({input_owner, face.id});
            result.geometry_effects.push_back(std::move(effect));
            outputs.push_back({"smooth", input_owner, refined.geometry});
        }
        result.produced_outputs.push_back({config.geometry_output_port,
            RuntimeValue::geometry_collection(std::move(outputs))});
        return result;
    };
}

} // namespace phoenix::smooth
