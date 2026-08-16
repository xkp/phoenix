#include "phoenix/execution.hpp"
#include "phoenix/migration/migrated_package_runtime_loader.hpp"
#include "phoenix/migration/production_migrated_package_io.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

const phoenix::RuntimeValue* find_input(
    const phoenix::InstructionExecutionFrame& frame,
    const phoenix::PortId& port)
{
    for (const auto& input : frame.inputs.promised_inputs) {
        if (input.port == port) {
            return &input.value;
        }
    }
    return nullptr;
}

std::filesystem::path temp_root()
{
    const auto root = std::filesystem::temp_directory_path() / "phoenix_p13_migrated_package_execution_smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

phoenix::migration::MigratedProjectPackage make_smoke_package()
{
    phoenix::migration::MigratedProjectPackage package;
    package.root_function_id = "ROOT@33333333-3333-3333-3333-333333333333";

    phoenix::migration::MigratedFunctionPackage function;
    function.graph.id = package.root_function_id;
    function.graph.input_ports.push_back(input_port("input", "geometry"));
    function.graph.output_ports.push_back(output_port("result", "geometry"));

    phoenix::InstructionDescriptor instruction;
    instruction.id = 10;
    instruction.kind = "p13_echo";
    instruction.input_ports.push_back(input_port("input", "geometry"));
    instruction.output_ports.push_back(output_port("output", "geometry"));
    instruction.has_else_port = false;
    function.graph.instructions.push_back(instruction);

    function.graph.edges.push_back(phoenix::EdgeDescriptor{1, "input", 10, "input"});
    function.graph.edges.push_back(phoenix::EdgeDescriptor{10, "output", 2, "result"});
    package.functions.emplace(package.root_function_id, function);
    return package;
}

bool output_has_geometry_label(
    const phoenix::FunctionExecutionResult& result,
    const phoenix::PortId& port,
    const std::string& label)
{
    for (const auto& output : result.outputs) {
        if (output.port != port) {
            continue;
        }
        const auto* geometry = output.value.as_geometry();
        return geometry != nullptr && geometry->debug_label == label;
    }
    return false;
}

bool test_read_load_execute_minimal_package()
{
    const auto root = temp_root();
    const auto package_path = root / "smoke.phxmig";

    const phoenix::migration::MigratedProjectPackageWriter writer;
    if (!writer.write(make_smoke_package(), package_path).empty()) {
        return false;
    }

    const phoenix::migration::MigratedProjectPackageReader reader;
    const auto read = reader.read(package_path);
    if (!read.ok()) {
        return false;
    }

    const phoenix::migration::MigratedPackageRuntimeLoader loader;
    const auto loaded = loader.load(read.package);
    if (!loaded.ok()) {
        return false;
    }

    const auto& function = loaded.package.functions.at(loaded.package.root_function_id).graph;
    phoenix::InstructionRegistry registry;
    registry.register_handler(
        "p13_echo",
        [](const phoenix::InstructionExecutionFrame& frame) {
            const auto* input = find_input(frame, "input");
            if (input == nullptr || input->as_geometry() == nullptr) {
                return phoenix::InstructionResult{frame.inputs.node_id, {}, "input missing"};
            }
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {phoenix::PortValue{"output", *input}},
                std::nullopt,
            };
        });

    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {phoenix::PortValue{"input", phoenix::RuntimeValue::geometry("fixture-input")}};
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 13;

    const phoenix::FunctionExecutor executor(registry);
    const auto result = executor.run(request);
    return result.status == phoenix::FunctionExecutionStatus::completed
        && !result.failure_message.has_value()
        && output_has_geometry_label(result, "result", "fixture-input");
}

} // namespace

int main()
{
    if (!test_read_load_execute_minimal_package()) {
        std::cerr << "migrated package execution smoke failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
