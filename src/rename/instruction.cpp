#include "phoenix/rename/instruction.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>

namespace phoenix::rename {
namespace {

const RuntimeValue* find_input(const InstructionExecutionFrame& frame, const PortId& port)
{
    for (const auto& input : frame.inputs.promised_inputs)
        if (input.port == port) return &input.value;
    return nullptr;
}

double length(const std::vector<RuntimeVertex>& vertices,
    const std::vector<RuntimeHalfedge>& edges, GeometryIndex edge_index)
{
    const auto& edge = edges[edge_index];
    const auto& next = edges[edge.next];
    const auto& a = vertices[edge.origin_vertex].point;
    const auto& b = vertices[next.origin_vertex].point;
    return std::hypot(std::hypot(b.x-a.x, b.y-a.y), b.z-a.z);
}

std::vector<GeometryIndex> face_edges(const std::vector<RuntimeFace>& faces,
    const std::vector<RuntimeHalfedge>& edges, GeometryIndex face)
{
    std::vector<GeometryIndex> result;
    auto current = faces[face].halfedge;
    do { result.push_back(current); current = edges[current].next; }
    while (current != faces[face].halfedge);
    return result;
}

bool base_edge_match(const std::vector<RuntimeFace>& faces,
    const std::vector<RuntimeHalfedge>& edges, GeometryIndex edge_index,
    const Condition& condition)
{
    const auto& edge = edges[edge_index];
    if (condition.from_label && edge.label != *condition.from_label) return false;
    if (condition.owning_face_label && faces[edge.face].label != *condition.owning_face_label)
        return false;
    if (condition.owning_edge_label && edge.label != *condition.owning_edge_label)
        return false;
    const auto is_border = edge.opposite == invalid_geometry_index;
    if (condition.border && is_border != *condition.border) return false;
    if (condition.opposite_face_label && (is_border
        || faces[edges[edge.opposite].face].label != *condition.opposite_face_label))
        return false;
    if (condition.opposite_edge_label && (is_border
        || edges[edge.opposite].label != *condition.opposite_edge_label)) return false;
    if (condition.adjacent_label1 || condition.adjacent_label2) {
        auto matches_adjacent = [&](GeometryIndex candidate) {
            if (candidate == invalid_geometry_index) return false;
            if (condition.adjacent_label1 && edges[candidate].label != *condition.adjacent_label1)
                return false;
            if (condition.adjacent_label2) {
                const auto other = edges[candidate].opposite;
                if (other == invalid_geometry_index || edges[other].label != *condition.adjacent_label2)
                    return false;
            }
            return true;
        };
        bool adjacent = false;
        if (condition.adjacent_relation == AdjacentRelation::previous
            || condition.adjacent_relation == AdjacentRelation::any) {
            const auto previous = std::find_if(
                edges.begin(),
                edges.end(),
                [edge_index, face = edge.face](const RuntimeHalfedge& candidate) {
                    return candidate.face == face && candidate.next == edge_index;
                });
            if (previous != edges.end()) {
                adjacent = adjacent || matches_adjacent(
                    static_cast<GeometryIndex>(std::distance(edges.begin(), previous)));
            }
        }
        if (condition.adjacent_relation == AdjacentRelation::next
            || condition.adjacent_relation == AdjacentRelation::any) {
            adjacent = adjacent || matches_adjacent(edge.next);
        }
        if (!adjacent) return false;
    }
    return true;
}

struct EvaluatedCondition {
    Condition source;
    double minimum_length = 0.0;
    double maximum_length = std::numeric_limits<double>::max();
};

CanonicalGeometryRef renamed(const CanonicalGeometry& source,
    const InstructionConfig& config,
    const std::vector<EvaluatedCondition>& conditions,
    std::uint64_t effective_seed)
{
    auto vertices = source.vertices();
    auto edges = source.halfedges();
    auto faces = source.faces();
    std::mt19937_64 random(config.seed ^ effective_seed);
    auto mapped = [&](LabelId original, std::optional<LabelId> fallback) {
        const auto found = config.label_map.find(original);
        if (found == config.label_map.end() || found->second.empty())
            return fallback.value_or(original);
        std::uniform_int_distribution<std::size_t> choose(0, found->second.size()-1);
        return found->second[choose(random)];
    };
    if (conditions.empty()) {
        for (auto& face : faces) face.label = mapped(face.label, config.all_faces_label);
        for (auto& edge : edges) edge.label = mapped(edge.label, config.all_edges_label);
    } else {
        std::vector<std::optional<LabelId>> face_changes(faces.size());
        std::vector<std::optional<LabelId>> edge_changes(edges.size());
        for (const auto& evaluated : conditions) {
            const auto& condition = evaluated.source;
            for (GeometryIndex face_index = 0; face_index < faces.size(); ++face_index) {
                const auto boundary = face_edges(faces, edges, face_index);
                if (condition.maximum_edge_count
                    && boundary.size() > *condition.maximum_edge_count) continue;
                if (condition.target == Target::faces) {
                    if (condition.from_label && faces[face_index].label != *condition.from_label)
                        continue;
                    std::vector<GeometryIndex> matching;
                    for (const auto edge : boundary) {
                        if (!base_edge_match(faces, edges, edge, condition)) continue;
                        const auto value = length(vertices, edges, edge);
                        if (value >= evaluated.minimum_length
                            && value <= evaluated.maximum_length) matching.push_back(edge);
                    }
                    if (condition.length_kind == LengthKind::largest
                        || condition.length_kind == LengthKind::smallest) {
                        const auto chosen = std::minmax_element(boundary.begin(), boundary.end(),
                            [&](auto a, auto b) { return length(vertices, edges, a)
                                < length(vertices, edges, b); });
                        const auto wanted = condition.length_kind == LengthKind::largest
                            ? *chosen.second : *chosen.first;
                        if (std::find(matching.begin(), matching.end(), wanted) == matching.end())
                            continue;
                    } else if (matching.empty() && condition.length_kind != LengthKind::none)
                        continue;
                    else if (condition.length_kind == LengthKind::none
                        && (condition.maximum_edge_count || condition.from_label)) {
                        // Face-only predicates already matched.
                    } else if (matching.empty()) continue;
                    face_changes[face_index] = condition.to_label;
                } else {
                    for (const auto edge_index : boundary) {
                        if (!base_edge_match(faces, edges, edge_index, condition)) continue;
                        const auto value = length(vertices, edges, edge_index);
                        if (value < evaluated.minimum_length
                            || value > evaluated.maximum_length) continue;
                        if (condition.length_kind == LengthKind::largest
                            || condition.length_kind == LengthKind::smallest) {
                            const auto chosen = std::minmax_element(boundary.begin(), boundary.end(),
                                [&](auto a, auto b) { return length(vertices, edges, a)
                                    < length(vertices, edges, b); });
                            const auto wanted = condition.length_kind == LengthKind::largest
                                ? *chosen.second : *chosen.first;
                            if (edge_index != wanted) continue;
                        }
                        edge_changes[edge_index] = condition.to_owning_face_label
                            ? faces[face_index].label : condition.to_label;
                    }
                }
            }
        }
        for (GeometryIndex i=0; i<faces.size(); ++i)
            faces[i].label = face_changes[i].value_or(config.all_faces_label.value_or(faces[i].label));
        for (GeometryIndex i=0; i<edges.size(); ++i)
            edges[i].label = edge_changes[i].value_or(config.all_edges_label.value_or(edges[i].label));
    }
    return CanonicalGeometry::create(std::move(vertices), std::move(edges), std::move(faces));
}

} // namespace

InstructionHandler make_instruction_handler(InstructionConfig config)
{
    return [config = std::move(config)](const InstructionExecutionFrame& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        const auto* value = find_input(frame, config.geometry_input_port);
        if (!value) { result.failure_message = "Rename requires canonical geometry input."; return result; }
        std::vector<GeometryValue> inputs;
        if (const auto* single = value->as_geometry()) {
            if (single->geometry) inputs.push_back(*single);
        } else if (const auto* collection = value->as_geometry_collection()) {
            inputs = collection->contributions;
        }
        std::vector<EvaluatedCondition> conditions;
        conditions.reserve(config.conditions.size());
        for (const auto& condition : config.conditions) {
            auto minimum = scripting::evaluate_numeric(condition.minimum_length, frame);
            if (minimum.error) {
                result.failure_message = *minimum.error;
                return result;
            }
            auto maximum = scripting::evaluate_numeric(condition.maximum_length, frame);
            if (maximum.error) {
                result.failure_message = *maximum.error;
                return result;
            }
            conditions.push_back(EvaluatedCondition{
                condition,
                minimum.value,
                maximum.value});
        }
        std::vector<GeometryValue> outputs;
        std::uint64_t item = 0;
        for (const auto& input : inputs) {
            if (!input.geometry) continue;
            auto geometry = renamed(*input.geometry, config,
                conditions,
                frame.effective_seed.value_or(frame.context.global_seed) ^ item++);
            if (!geometry) { result.failure_message = "Rename produced invalid geometry."; return result; }
            outputs.push_back({"rename", input.accumulation_actor_id, std::move(geometry)});
        }
        result.produced_outputs.push_back({config.geometry_output_port,
            RuntimeValue::geometry_collection(std::move(outputs))});
        return result;
    };
}

} // namespace phoenix::rename
