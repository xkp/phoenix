#include "phoenix/graph.hpp"

#include <functional>
#include <sstream>
#include <unordered_set>

namespace phoenix {

namespace {

using NodeLookup = std::unordered_map<NodeId, const InstructionDescriptor*>;

void add_issue(
    GraphValidationResult& result,
    GraphValidationCode code,
    std::string message,
    std::optional<NodeId> node_id = std::nullopt,
    std::optional<std::size_t> edge_index = std::nullopt)
{
    result.issues.push_back(GraphValidationIssue{
        code,
        std::move(message),
        node_id,
        edge_index,
    });
}

bool has_duplicate_ports(
    const std::vector<PortDescriptor>& ports,
    GraphValidationResult& result,
    GraphValidationCode duplicate_code,
    const std::string& owner_label,
    std::optional<NodeId> node_id = std::nullopt)
{
    bool has_duplicates = false;
    std::unordered_set<PortId> seen;

    for (const auto& port : ports) {
        if (!seen.insert(port.id).second) {
            std::ostringstream stream;
            stream << "Duplicate port '" << port.id << "' on " << owner_label << '.';
            add_issue(result, duplicate_code, stream.str(), node_id);
            has_duplicates = true;
        }
    }

    return has_duplicates;
}

void validate_port_directions(
    const std::vector<PortDescriptor>& ports,
    PortDirection expected_direction,
    GraphValidationResult& result,
    GraphValidationCode code,
    const std::string& owner_label,
    std::optional<NodeId> node_id = std::nullopt)
{
    for (const auto& port : ports) {
        if (port.direction != expected_direction) {
            std::ostringstream stream;
            stream << "Port '" << port.id << "' on " << owner_label << " has wrong direction.";
            add_issue(result, code, stream.str(), node_id);
        }
    }
}

const PortDescriptor* find_port(
    const std::vector<PortDescriptor>& ports,
    const PortId& id) noexcept
{
    for (const auto& port : ports) {
        if (port.id == id) {
            return &port;
        }
    }

    return nullptr;
}

NodeLookup build_node_lookup(
    const FunctionDescriptor& function,
    GraphValidationResult& result)
{
    NodeLookup nodes;

    for (const auto& instruction : function.instructions) {
        if (!nodes.emplace(instruction.id, &instruction).second) {
            std::ostringstream stream;
            stream << "Duplicate instruction id '" << instruction.id << "'.";
            add_issue(result, GraphValidationCode::duplicate_instruction_id, stream.str(), instruction.id);
        }
    }

    return nodes;
}

void validate_edges(
    const FunctionDescriptor& function,
    const NodeLookup& nodes,
    GraphValidationResult& result)
{
    for (std::size_t edge_index = 0; edge_index < function.edges.size(); ++edge_index) {
        const auto& edge = function.edges[edge_index];

        const auto source_it = nodes.find(edge.from_node);
        if (source_it == nodes.end()) {
            std::ostringstream stream;
            stream << "Edge " << edge_index << " references missing source node '" << edge.from_node << "'.";
            add_issue(
                result,
                GraphValidationCode::edge_references_missing_source_node,
                stream.str(),
                std::nullopt,
                edge_index);
            continue;
        }

        const auto target_it = nodes.find(edge.to_node);
        if (target_it == nodes.end()) {
            std::ostringstream stream;
            stream << "Edge " << edge_index << " references missing target node '" << edge.to_node << "'.";
            add_issue(
                result,
                GraphValidationCode::edge_references_missing_target_node,
                stream.str(),
                std::nullopt,
                edge_index);
            continue;
        }

        const auto* source_port = find_port(source_it->second->output_ports, edge.from_port);
        if (source_port == nullptr) {
            std::ostringstream stream;
            stream << "Edge " << edge_index << " references missing source port '" << edge.from_port << "'.";
            add_issue(
                result,
                GraphValidationCode::edge_references_missing_source_port,
                stream.str(),
                edge.from_node,
                edge_index);
            continue;
        }

        if (source_port->direction != PortDirection::output) {
            std::ostringstream stream;
            stream << "Edge " << edge_index << " uses non-output source port '" << edge.from_port << "'.";
            add_issue(
                result,
                GraphValidationCode::edge_uses_non_output_source_port,
                stream.str(),
                edge.from_node,
                edge_index);
        }

        const auto* target_port = find_port(target_it->second->input_ports, edge.to_port);
        if (target_port == nullptr) {
            std::ostringstream stream;
            stream << "Edge " << edge_index << " references missing target port '" << edge.to_port << "'.";
            add_issue(
                result,
                GraphValidationCode::edge_references_missing_target_port,
                stream.str(),
                edge.to_node,
                edge_index);
            continue;
        }

        if (target_port->direction != PortDirection::input) {
            std::ostringstream stream;
            stream << "Edge " << edge_index << " uses non-input target port '" << edge.to_port << "'.";
            add_issue(
                result,
                GraphValidationCode::edge_uses_non_input_target_port,
                stream.str(),
                edge.to_node,
                edge_index);
        }

        if (source_port->type != target_port->type) {
            std::ostringstream stream;
            stream << "Edge " << edge_index << " connects incompatible port types '"
                   << source_port->type << "' -> '" << target_port->type << "'.";
            add_issue(
                result,
                GraphValidationCode::edge_type_mismatch,
                stream.str(),
                std::nullopt,
                edge_index);
        }
    }
}

void validate_instruction_rules(
    const FunctionDescriptor& function,
    GraphValidationResult& result)
{
    bool has_actor_instruction = false;

    for (const auto& instruction : function.instructions) {
        has_duplicate_ports(
            instruction.input_ports,
            result,
            GraphValidationCode::duplicate_instruction_input_port,
            "instruction input ports",
            instruction.id);
        validate_port_directions(
            instruction.input_ports,
            PortDirection::input,
            result,
            GraphValidationCode::instruction_input_port_wrong_direction,
            "instruction input ports",
            instruction.id);
        has_duplicate_ports(
            instruction.output_ports,
            result,
            GraphValidationCode::duplicate_instruction_output_port,
            "instruction output ports",
            instruction.id);
        validate_port_directions(
            instruction.output_ports,
            PortDirection::output,
            result,
            GraphValidationCode::instruction_output_port_wrong_direction,
            "instruction output ports",
            instruction.id);

        has_actor_instruction = has_actor_instruction || instruction.generates_actor;

        if (instruction.has_else_port && find_port(instruction.output_ports, "else") == nullptr) {
            std::ostringstream stream;
            stream << "Instruction '" << instruction.id << "' requires an else port but none was declared.";
            add_issue(
                result,
                GraphValidationCode::instruction_missing_else_port,
                stream.str(),
                instruction.id);
        }
    }

    if (function.generates_actor && !has_actor_instruction && !function.instructions.empty()) {
        add_issue(
            result,
            GraphValidationCode::function_generates_actor_without_actor_instruction,
            "Actor-generating function must contain at least one actor-generating instruction.");
    }
}

void validate_cycles(
    const FunctionDescriptor& function,
    const NodeLookup& nodes,
    GraphValidationResult& result)
{
    std::unordered_map<NodeId, std::vector<NodeId>> adjacency;

    for (const auto& pair : nodes) {
        adjacency.emplace(pair.first, std::vector<NodeId>{});
    }

    for (const auto& edge : function.edges) {
        if (nodes.find(edge.from_node) != nodes.end() && nodes.find(edge.to_node) != nodes.end()) {
            adjacency[edge.from_node].push_back(edge.to_node);
        }
    }

    enum class VisitState {
        unvisited,
        visiting,
        done,
    };

    std::unordered_map<NodeId, VisitState> visit_states;
    for (const auto& pair : nodes) {
        visit_states.emplace(pair.first, VisitState::unvisited);
    }

    bool found_cycle = false;
    std::function<void(NodeId)> dfs = [&](NodeId node_id) {
        if (found_cycle) {
            return;
        }

        visit_states[node_id] = VisitState::visiting;

        for (const auto next : adjacency[node_id]) {
            const auto next_state = visit_states[next];
            if (next_state == VisitState::visiting) {
                found_cycle = true;
                return;
            }
            if (next_state == VisitState::unvisited) {
                dfs(next);
                if (found_cycle) {
                    return;
                }
            }
        }

        visit_states[node_id] = VisitState::done;
    };

    for (const auto& pair : nodes) {
        if (visit_states[pair.first] == VisitState::unvisited) {
            dfs(pair.first);
        }

        if (found_cycle) {
            add_issue(
                result,
                GraphValidationCode::cycle_detected,
                "Cycle detected in function graph. Version one requires acyclic graphs.");
            return;
        }
    }
}

} // namespace

GraphValidationResult GraphValidator::validate(const FunctionDescriptor& function) const
{
    GraphValidationResult result;

    has_duplicate_ports(
        function.input_ports,
        result,
        GraphValidationCode::duplicate_function_input_port,
        "function input ports");
    validate_port_directions(
        function.input_ports,
        PortDirection::input,
        result,
        GraphValidationCode::function_input_port_wrong_direction,
        "function input ports");
    has_duplicate_ports(
        function.output_ports,
        result,
        GraphValidationCode::duplicate_function_output_port,
        "function output ports");
    validate_port_directions(
        function.output_ports,
        PortDirection::output,
        result,
        GraphValidationCode::function_output_port_wrong_direction,
        "function output ports");

    const auto nodes = build_node_lookup(function, result);
    validate_instruction_rules(function, result);
    validate_edges(function, nodes, result);
    validate_cycles(function, nodes, result);

    return result;
}

std::string to_string(GraphValidationCode code)
{
    switch (code) {
    case GraphValidationCode::duplicate_function_input_port:
        return "duplicate_function_input_port";
    case GraphValidationCode::duplicate_function_output_port:
        return "duplicate_function_output_port";
    case GraphValidationCode::duplicate_instruction_id:
        return "duplicate_instruction_id";
    case GraphValidationCode::function_input_port_wrong_direction:
        return "function_input_port_wrong_direction";
    case GraphValidationCode::function_output_port_wrong_direction:
        return "function_output_port_wrong_direction";
    case GraphValidationCode::duplicate_instruction_input_port:
        return "duplicate_instruction_input_port";
    case GraphValidationCode::duplicate_instruction_output_port:
        return "duplicate_instruction_output_port";
    case GraphValidationCode::instruction_input_port_wrong_direction:
        return "instruction_input_port_wrong_direction";
    case GraphValidationCode::instruction_output_port_wrong_direction:
        return "instruction_output_port_wrong_direction";
    case GraphValidationCode::edge_references_missing_source_node:
        return "edge_references_missing_source_node";
    case GraphValidationCode::edge_references_missing_target_node:
        return "edge_references_missing_target_node";
    case GraphValidationCode::edge_references_missing_source_port:
        return "edge_references_missing_source_port";
    case GraphValidationCode::edge_references_missing_target_port:
        return "edge_references_missing_target_port";
    case GraphValidationCode::edge_uses_non_output_source_port:
        return "edge_uses_non_output_source_port";
    case GraphValidationCode::edge_uses_non_input_target_port:
        return "edge_uses_non_input_target_port";
    case GraphValidationCode::edge_type_mismatch:
        return "edge_type_mismatch";
    case GraphValidationCode::instruction_missing_else_port:
        return "instruction_missing_else_port";
    case GraphValidationCode::function_generates_actor_without_actor_instruction:
        return "function_generates_actor_without_actor_instruction";
    case GraphValidationCode::cycle_detected:
        return "cycle_detected";
    }

    return "unknown";
}

std::string format_issue(const GraphValidationIssue& issue)
{
    std::ostringstream stream;
    stream << to_string(issue.code) << ": " << issue.message;

    if (issue.node_id.has_value()) {
        stream << " [node=" << *issue.node_id << "]";
    }

    if (issue.edge_index.has_value()) {
        stream << " [edge=" << *issue.edge_index << "]";
    }

    return stream.str();
}

} // namespace phoenix
