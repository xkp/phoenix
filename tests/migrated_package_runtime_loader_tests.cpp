#include "phoenix/migration/migrated_package_runtime_loader.hpp"

#include <cstdlib>
#include <iostream>

namespace {

phoenix::PortDescriptor input_port(const phoenix::PortId& id, const phoenix::TypeId& type)
{
    return phoenix::PortDescriptor{id, type, phoenix::PortDirection::input};
}

phoenix::PortDescriptor output_port(const phoenix::PortId& id, const phoenix::TypeId& type)
{
    return phoenix::PortDescriptor{id, type, phoenix::PortDirection::output};
}

bool test_boundary_edges_are_normalized()
{
    phoenix::migration::MigratedProjectPackage package;
    package.root_function_id = "ROOT@11111111-1111-1111-1111-111111111111";

    phoenix::migration::MigratedFunctionPackage function;
    function.graph.id = package.root_function_id;
    function.graph.input_ports.push_back(input_port("0:input", "geometry"));
    function.graph.output_ports.push_back(output_port("0:output", "geometry"));

    phoenix::InstructionDescriptor instruction;
    instruction.id = 10;
    instruction.kind = "passthrough";
    instruction.input_ports.push_back(input_port("0:input", "geometry"));
    instruction.output_ports.push_back(output_port("0:output", "geometry"));
    function.graph.instructions.push_back(instruction);

    function.graph.edges.push_back(phoenix::EdgeDescriptor{1, "0:input", 10, "0:input"});
    function.graph.edges.push_back(phoenix::EdgeDescriptor{10, "0:output", 2, "0:output"});
    package.functions.emplace(package.root_function_id, function);

    const phoenix::migration::MigratedPackageRuntimeLoader loader;
    const auto result = loader.load(package);
    const auto& loaded = result.package.functions.at(package.root_function_id).graph;
    return result.ok()
        && loaded.output_node_id.has_value()
        && *loaded.output_node_id == 2
        && loaded.edges.size() == 1;
}

bool test_invalid_internal_edge_is_reported()
{
    phoenix::migration::MigratedProjectPackage package;
    package.root_function_id = "ROOT@22222222-2222-2222-2222-222222222222";

    phoenix::migration::MigratedFunctionPackage function;
    function.graph.id = package.root_function_id;

    phoenix::InstructionDescriptor instruction;
    instruction.id = 10;
    instruction.kind = "source";
    instruction.output_ports.push_back(output_port("0:output", "geometry"));
    function.graph.instructions.push_back(instruction);

    function.graph.edges.push_back(phoenix::EdgeDescriptor{10, "0:output", 99, "0:input"});
    package.functions.emplace(package.root_function_id, function);

    const phoenix::migration::MigratedPackageRuntimeLoader loader;
    const auto result = loader.load(package);
    return !result.ok();
}

} // namespace

int main()
{
    if (!test_boundary_edges_are_normalized()) {
        std::cerr << "boundary edge normalization test failed\n";
        return EXIT_FAILURE;
    }
    if (!test_invalid_internal_edge_is_reported()) {
        std::cerr << "invalid internal edge test failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
