#include "phoenix/migration/migrated_package_runtime_loader.hpp"

#include "phoenix/graph.hpp"

#include <algorithm>
#include <sstream>
#include <optional>
#include <unordered_set>

namespace phoenix::migration {

namespace {

std::unordered_set<NodeId> instruction_ids(const FunctionDescriptor& function)
{
    std::unordered_set<NodeId> ids;
    for (const auto& instruction : function.instructions) {
        ids.insert(instruction.id);
    }
    return ids;
}

bool has_port(const std::vector<PortDescriptor>& ports, const PortId& id)
{
    for (const auto& port : ports) {
        if (port.id == id) {
            return true;
        }
    }
    return false;
}

std::optional<PortDescriptor> port_descriptor(
    const std::vector<PortDescriptor>& ports,
    const PortId& id)
{
    for (const auto& port : ports) {
        if (port.id == id) return port;
    }
    return std::nullopt;
}

bool is_function_input_boundary_edge(
    const FunctionDescriptor& function,
    const std::unordered_set<NodeId>& ids,
    const EdgeDescriptor& edge)
{
    return ids.find(edge.from_node) == ids.end()
        && ids.find(edge.to_node) != ids.end()
        && has_port(function.input_ports, edge.from_port);
}

bool is_function_output_boundary_edge(
    const FunctionDescriptor& function,
    const std::unordered_set<NodeId>& ids,
    const EdgeDescriptor& edge)
{
    return ids.find(edge.from_node) != ids.end()
        && ids.find(edge.to_node) == ids.end()
        && has_port(function.output_ports, edge.to_port);
}

std::optional<NodeId> infer_output_node_id(
    const FunctionDescriptor& function,
    const std::unordered_set<NodeId>& ids)
{
    std::optional<NodeId> output_node_id = function.output_node_id;
    for (const auto& edge : function.edges) {
        if (!is_function_output_boundary_edge(function, ids, edge)) {
            continue;
        }
        if (output_node_id.has_value() && *output_node_id != edge.to_node) {
            return std::nullopt;
        }
        output_node_id = edge.to_node;
    }
    return output_node_id;
}

void materialize_output_node(FunctionDescriptor& function, NodeId output_node_id)
{
    for (const auto& instruction : function.instructions) {
        if (instruction.id == output_node_id) {
            function.output_node_id = output_node_id;
            return;
        }
    }

    InstructionDescriptor output_node;
    output_node.id = output_node_id;
    output_node.kind = "output";
    output_node.has_else_port = false;
    for (const auto& port : function.output_ports) {
        output_node.input_ports.push_back(PortDescriptor{
            port.id,
            port.type,
            PortDirection::input,
        });
    }
    function.instructions.push_back(std::move(output_node));
    function.output_node_id = output_node_id;
}

void add_unique_port(std::vector<PortDescriptor>& ports, PortDescriptor port)
{
    for (const auto& existing : ports) {
        if (existing.id == port.id) return;
    }
    ports.push_back(std::move(port));
}

void materialize_input_nodes(
    FunctionDescriptor& function,
    const FunctionDescriptor& source,
    const std::unordered_set<NodeId>& ids)
{
    for (const auto& edge : source.edges) {
        if (!is_function_input_boundary_edge(source, ids, edge)) continue;

        auto existing = std::find_if(
            function.instructions.begin(),
            function.instructions.end(),
            [&](const InstructionDescriptor& instruction) {
                return instruction.id == edge.from_node;
            });
        if (existing == function.instructions.end()) {
            InstructionDescriptor input_node;
            input_node.id = edge.from_node;
            input_node.kind = "input";
            input_node.has_else_port = false;
            input_node.consumes_geometry = false;
            function.instructions.push_back(std::move(input_node));
            existing = std::prev(function.instructions.end());
        }

        auto port = port_descriptor(source.input_ports, edge.from_port)
            .value_or(PortDescriptor{edge.from_port, "geometry", PortDirection::input});
        port.direction = PortDirection::input;
        add_unique_port(existing->input_ports, port);
        port.direction = PortDirection::output;
        add_unique_port(existing->output_ports, port);
    }
}

FunctionDescriptor normalize_graph_for_runtime_validation(const FunctionDescriptor& source)
{
    auto normalized = source;
    const auto ids = instruction_ids(source);

    for (auto& instruction : normalized.instructions) {
        instruction.has_else_port = false;
    }

    const auto output_node_id = infer_output_node_id(source, ids);
    if (output_node_id.has_value()) {
        materialize_output_node(normalized, *output_node_id);
    }
    materialize_input_nodes(normalized, source, ids);

    normalized.edges.clear();
    for (const auto& edge : source.edges) {
        normalized.edges.push_back(edge);
    }

    return normalized;
}

void add_graph_validation_diagnostics(
    MigratedPackageLoadResult& result,
    const FunctionId& function_id,
    const GraphValidationResult& validation)
{
    for (const auto& issue : validation.issues) {
        std::ostringstream stream;
        stream << format_issue(issue);
        result.diagnostics.push_back(MigratedPackageLoadDiagnostic{
            MigratedPackageLoadDiagnosticCode::graph_validation_failed,
            function_id,
            stream.str(),
        });
    }
}

} // namespace

MigratedPackageLoadResult MigratedPackageRuntimeLoader::load(const MigratedProjectPackage& package) const
{
    MigratedPackageLoadResult result;
    result.package.root_function_id = package.root_function_id;
    result.package.label_ids = package.label_ids;
    result.package.functions = package.functions;

    if (package.functions.find(package.root_function_id) == package.functions.end()) {
        result.diagnostics.push_back(MigratedPackageLoadDiagnostic{
            MigratedPackageLoadDiagnosticCode::missing_root_function,
            package.root_function_id,
            "Root function is not present in the package function table.",
        });
    }

    const GraphValidator validator;
    for (auto& entry : result.package.functions) {
        entry.second.graph = normalize_graph_for_runtime_validation(entry.second.graph);
        const auto validation = validator.validate(entry.second.graph);
        add_graph_validation_diagnostics(result, entry.first, validation);
    }

    return result;
}

std::string to_string(MigratedPackageLoadDiagnosticCode code)
{
    switch (code) {
    case MigratedPackageLoadDiagnosticCode::missing_root_function:
        return "missing_root_function";
    case MigratedPackageLoadDiagnosticCode::graph_validation_failed:
        return "graph_validation_failed";
    }

    return "unknown";
}

} // namespace phoenix::migration
