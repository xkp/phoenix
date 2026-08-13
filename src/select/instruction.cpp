#include "phoenix/select/instruction.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <random>
#include <utility>

namespace phoenix::select {
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
    (void)frame;
    return ActorId{"root"};
}

struct Candidate {
    GeometryValue source;
    GeometryIndex face = invalid_geometry_index;
};

double edge_length(const CanonicalGeometry& geometry, GeometryIndex halfedge)
{
    const auto& edge = geometry.halfedges()[halfedge];
    const auto& next = geometry.halfedges()[edge.next];
    const auto& source = geometry.vertices()[edge.origin_vertex].point;
    const auto& target = geometry.vertices()[next.origin_vertex].point;
    return std::hypot(std::hypot(target.x - source.x, target.y - source.y),
        target.z - source.z);
}

bool matches(const CanonicalGeometry& geometry, GeometryIndex face_index,
    const FaceCondition& condition)
{
    const auto& face = geometry.faces()[face_index];
    if (condition.face_label && face.label != *condition.face_label) return false;
    const bool needs_edge = condition.edge_label || condition.opposite_face_label
        || condition.opposite_edge_label || condition.require_border_edge
        || condition.minimum_edge_length > 0.0
        || condition.maximum_edge_length < std::numeric_limits<double>::max();
    if (!needs_edge) return true;
    auto current = face.halfedge;
    do {
        const auto& edge = geometry.halfedges()[current];
        if (condition.edge_label && edge.label != *condition.edge_label) {
            current = edge.next;
            continue;
        }
        const auto border = edge.opposite == invalid_geometry_index;
        if (condition.require_border_edge && !border) {
            current = edge.next;
            continue;
        }
        if (condition.opposite_face_label) {
            if (border || geometry.faces()[geometry.halfedges()[edge.opposite].face].label
                    != *condition.opposite_face_label) {
                current = edge.next;
                continue;
            }
        }
        if (condition.opposite_edge_label
            && (border || geometry.halfedges()[edge.opposite].label
                    != *condition.opposite_edge_label)) {
            current = edge.next;
            continue;
        }
        const auto length = edge_length(geometry, current);
        if (length >= condition.minimum_edge_length
            && length <= condition.maximum_edge_length) return true;
        current = edge.next;
    } while (current != face.halfedge);
    return false;
}

} // namespace

InstructionHandler make_instruction_handler(InstructionConfig config)
{
    return [config = std::move(config)](const InstructionExecutionFrame& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        const auto* value = find_input(frame, config.geometry_input_port);
        if (!value) {
            result.failure_message = "Select requires canonical geometry input.";
            return result;
        }
        std::vector<GeometryValue> inputs;
        if (const auto* single = value->as_geometry()) {
            if (single->geometry) inputs.push_back(*single);
        } else if (const auto* collection = value->as_geometry_collection()) {
            for (const auto& contribution : collection->contributions)
                if (contribution.geometry) inputs.push_back(contribution);
        }
        if (inputs.empty()) {
            result.failure_message = "Select requires at least one canonical geometry input.";
            return result;
        }

        std::vector<Candidate> candidates;
        for (const auto& input : inputs)
            for (GeometryIndex face = 0; face < input.geometry->faces().size(); ++face)
                candidates.push_back({input, face});

        auto remaining = config.limit.count;
        std::mt19937_64 random(config.limit.seed
            ^ frame.effective_seed.value_or(frame.context.global_seed));
        if (remaining >= 0 && config.limit.random_range >= 0) {
            const auto step = std::max<std::int64_t>(1, config.limit.random_step);
            const auto slots = config.limit.random_range / step;
            std::uniform_int_distribution<std::int64_t> distribution(0, slots);
            remaining += distribution(random) * step;
        }
        if (config.limit.percentage && remaining >= 0)
            remaining = remaining * static_cast<std::int64_t>(candidates.size()) / 100;
        if (remaining > 0) std::shuffle(candidates.begin(), candidates.end(), random);

        std::map<PortId, std::vector<GeometryValue>> routed;
        for (const auto& candidate : candidates) {
            PortId output = config.default_output_port;
            bool matched = false;
            if (!config.label_routes.empty()) {
                const auto label = candidate.source.geometry->faces()[candidate.face].label;
                for (const auto& route : config.label_routes) {
                    if (route.label != label) continue;
                    output = route.output.empty() ? config.default_output_port : route.output;
                    matched = true;
                    break;
                }
            } else {
                matched = std::all_of(config.conditions.begin(), config.conditions.end(),
                    [&](const FaceCondition& condition) {
                        return matches(*candidate.source.geometry, candidate.face, condition);
                    });
            }
            if (!matched) output = config.else_output_port;
            if (remaining >= 0 && (remaining <= 0 || output == config.else_output_port))
                output = config.else_output_port;
            else if (remaining > 0) --remaining;
            routed[output].push_back({"select", owner(frame, candidate.source),
                candidate.source.geometry->copy_face(candidate.face)});
        }
        for (auto& output : routed)
            result.produced_outputs.push_back({output.first,
                RuntimeValue::geometry_collection(std::move(output.second))});
        return result;
    };
}

} // namespace phoenix::select
