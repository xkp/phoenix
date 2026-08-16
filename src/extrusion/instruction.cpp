#include "phoenix/extrusion/instruction.hpp"

#include <CGAL/enum.h>

#include <chrono>
#include <cmath>
#include <utility>

namespace phoenix::extrusion {
namespace {

constexpr double pi = 3.14159265358979323846;

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

std::optional<ProfileRef> evaluate_profile(
    const InstructionConfig& config,
    const InstructionExecutionFrame& frame,
    std::string& error)
{
    if (config.runtime_profile.empty()) return config.profile;
    std::vector<ProfileSegment> segments;
    segments.reserve(config.runtime_profile.size());
    CGAL::Sign sign = CGAL::ZERO;
    for (const auto& source : config.runtime_profile) {
        auto x = scripting::evaluate_numeric_range(source.delta_x, frame);
        if (x.error) {
            error = *x.error;
            return std::nullopt;
        }
        auto y = scripting::evaluate_numeric_range(source.delta_y, frame);
        if (y.error) {
            error = *y.error;
            return std::nullopt;
        }
        ProfileSegment segment;
        segment.delta_x = x.value;
        segment.delta_y = y.value;
        auto optional = [&](const std::optional<scripting::NumericRange>& value)
            -> std::optional<double> {
            if (!value) return std::nullopt;
            auto evaluated = scripting::evaluate_numeric_range(*value, frame);
            if (evaluated.error) {
                error = *evaluated.error;
                return std::nullopt;
            }
            return evaluated.value;
        };
        const auto height = optional(source.height);
        if (!error.empty()) return std::nullopt;
        const auto width = optional(source.width);
        if (!error.empty()) return std::nullopt;
        const auto length = optional(source.length);
        if (!error.empty()) return std::nullopt;
        const auto angle = optional(source.angle_degrees);
        if (!error.empty()) return std::nullopt;
        const auto xsign = segment.delta_x < 0.0 ? -1.0 : 1.0;
        const auto ysign = segment.delta_y < 0.0 ? -1.0 : 1.0;
        if (height && width) {
            segment.delta_x = xsign * *width;
            segment.delta_y = ysign * *height;
        } else if (height && angle) {
            if (*angle < 0.0 || *angle > 90.0) {
                error = "Profile angle must be in [0, 90].";
                return std::nullopt;
            }
            const auto tangent = std::tan(*angle * pi / 180.0);
            segment.delta_x = tangent == 0.0 ? segment.delta_x : xsign * *height / tangent;
            segment.delta_y = ysign * *height;
        } else if (width && length) {
            if (*length < *width) {
                error = "Profile length must be greater than or equal to width.";
                return std::nullopt;
            }
            segment.delta_x = xsign * *width;
            segment.delta_y = ysign * std::sqrt(*length * *length - *width * *width);
        } else if (angle && length) {
            const auto radians = *angle * pi / 180.0;
            segment.delta_x = xsign * *length * std::cos(radians);
            segment.delta_y = ysign * *length * std::sin(radians);
        }
        segment.face_label = source.face_label;
        segment.left_label = source.left_label;
        segment.bottom_label = source.bottom_label;
        segment.right_label = source.right_label;
        segment.top_label = source.top_label;
        segment.skirt_label = source.skirt_label;
        segment.horizontal = std::abs(segment.delta_y) < 1e-5;
        if (segment.delta_y > 0.0) sign = CGAL::POSITIVE;
        if (segment.delta_y < 0.0) sign = CGAL::NEGATIVE;
        segments.push_back(segment);
    }
    auto profile = Profile::create(std::move(segments), sign);
    if (!profile) error = "Extrusion runtime profile evaluated to an invalid profile.";
    return profile;
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
        std::string profile_error;
        const auto profile = evaluate_profile(config, frame, profile_error);
        if (!profile) {
            result.failure_message = "Extrusion requires an immutable profile.";
            if (!profile_error.empty()) result.failure_message = profile_error;
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
                const auto input = make_kernel_input(prepared.face, *profile,
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
