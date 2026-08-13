#include "phoenix/extrusion/instruction.hpp"

#include <chrono>
#include <utility>

namespace phoenix::extrusion {
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
            result.failure_message = "Extrusion requires one canonical geometry input.";
            return result;
        }
        if (!config.profile) {
            result.failure_message = "Extrusion requires an immutable profile.";
            return result;
        }
        if (!frame.element_ids) {
            result.failure_message = "Extrusion requires the run-scoped element ID allocator.";
            return result;
        }

        const auto owner = geometry_owner(frame, *geometry);
        StageMetrics metrics;
        std::vector<GeometryValue> outputs;
        for (GeometryIndex face_index = 0;
             face_index < geometry->geometry->faces().size(); ++face_index) {
            const auto source_face = geometry->geometry->copy_face(face_index);
            GeometryItemEffect effect;
            effect.item_key = face_index;
            ++metrics.item_count;
            const auto preparation_start = std::chrono::steady_clock::now();
            const auto prepared = ExtrusionInputAdapter{}.prepare_face(
                *geometry->geometry, face_index);
            metrics.preparation_microseconds += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - preparation_start).count());
            if (!prepared.success) {
                effect.succeeded = false;
                effect.failure_message = prepared.diagnostics.empty()
                    ? "Could not prepare extrusion source face."
                    : prepared.diagnostics.front().message;
            } else {
                const auto input = make_kernel_input(prepared.face, config.profile,
                    config.bottom_label, config.right_label, config.top_label,
                    config.left_label, config.skirt_label, config.cap_label);
                if (!input) {
                    effect.succeeded = false;
                    effect.failure_message = "Could not construct the extrusion kernel input.";
                } else {
                    const auto kernel_start = std::chrono::steady_clock::now();
                    const auto kernel = run_kernel(*input, *frame.element_ids);
                    metrics.kernel_microseconds += static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - kernel_start).count());
                    if (!kernel.success) {
                        effect.succeeded = false;
                        effect.failure_message = kernel.diagnostics.empty()
                            ? "Extrusion kernel failed."
                            : kernel.diagnostics.front().message;
                    } else {
                        const auto demotion_start = std::chrono::steady_clock::now();
                        const auto demoted = SurfaceMeshAdapter{}.demote(kernel.working);
                        metrics.demotion_microseconds += static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - demotion_start).count());
                        if (!demoted.success()) {
                            effect.succeeded = false;
                            effect.failure_message = demoted.diagnostics.empty()
                                ? "Could not demote extrusion output."
                                : demoted.diagnostics.front().message;
                        } else {
                            const auto repair_start = std::chrono::steady_clock::now();
                            const auto repaired = GeometryRepairer{config.repair_policy}.repair(
                                *demoted.geometry);
                            metrics.repair_microseconds += static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - repair_start).count());
                            if (!repaired.success()) {
                                effect.succeeded = false;
                                effect.failure_message = "Could not repair extrusion output.";
                            } else {
                                effect.generated_geometry = repaired.geometry;
                                effect.consumed_faces.push_back(
                                    {owner, prepared.face.source_face_id});
                                outputs.push_back(GeometryValue{
                                    "extrusion", owner, repaired.geometry});
                                ++metrics.succeeded_item_count;
                            }
                        }
                    }
                }
            }
            if (!effect.succeeded) {
                result.failures.push_back(item_failure(
                    frame, config, *geometry, source_face,
                    effect.item_key, effect.failure_message));
            }
            result.geometry_effects.push_back(std::move(effect));
        }
        result.produced_outputs.push_back({config.geometry_output_port,
            RuntimeValue::geometry_collection(std::move(outputs))});
        if (config.metrics_sink) config.metrics_sink->record_extrusion_stages(metrics);
        return result;
    };
}

} // namespace phoenix::extrusion
