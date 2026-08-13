#include "phoenix/partition/instruction.hpp"
#include "phoenix/partition/ported/production/partition_solver.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

phoenix::CanonicalGeometryRef rectangle()
{
    using namespace phoenix;
    return CanonicalGeometry::create({
        {{0, 0, 0}, VertexId{1}, 0}, {{4, 0, 0}, VertexId{2}, 1},
        {{4, 3, 0}, VertexId{3}, 2}, {{0, 3, 0}, VertexId{4}, 3}}, {
        {0, 0, 1, 3, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{10}, EdgeId{20}, LabelId{30}},
        {1, 0, 2, 0, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{11}, EdgeId{21}, LabelId{31}},
        {2, 0, 3, 1, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{12}, EdgeId{22}, LabelId{32}},
        {3, 0, 0, 2, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{13}, EdgeId{23}, LabelId{33}}},
        {{0, FaceId{40}, LabelId{41}}});
}

phoenix::CanonicalGeometryRef two_rectangles()
{
    using namespace phoenix;
    return CanonicalGeometry::create({
        {{0, 0, 0}, VertexId{1}, 0}, {{4, 0, 0}, VertexId{2}, 1},
        {{4, 3, 0}, VertexId{3}, 2}, {{0, 3, 0}, VertexId{4}, 3},
        {{6, 0, 0}, VertexId{5}, 4}, {{10, 0, 0}, VertexId{6}, 5},
        {{10, 3, 0}, VertexId{7}, 6}, {{6, 3, 0}, VertexId{8}, 7}}, {
        {0, 0, 1, 3, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{10}, EdgeId{20}, LabelId{30}},
        {1, 0, 2, 0, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{11}, EdgeId{21}, LabelId{31}},
        {2, 0, 3, 1, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{12}, EdgeId{22}, LabelId{32}},
        {3, 0, 0, 2, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{13}, EdgeId{23}, LabelId{33}},
        {4, 1, 5, 7, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{14}, EdgeId{24}, LabelId{30}},
        {5, 1, 6, 4, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{15}, EdgeId{25}, LabelId{31}},
        {6, 1, 7, 5, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{16}, EdgeId{26}, LabelId{32}},
        {7, 1, 4, 6, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{17}, EdgeId{27}, LabelId{33}}},
        {{0, FaceId{40}, LabelId{41}}, {4, FaceId{42}, LabelId{43}}});
}

phoenix::partition::ProductionModelRef one_cut_model()
{
    auto cut = std::make_shared<partition_cut>(nullptr);
    cut->id = 0;
    cut->segment = cut_segment_id(2);
    cut->src = cut_segment_id(0);
    cut->dst = cut_segment_id(1);
    cut->faceLeft = 50;
    cut->faceRight = 51;
    cut->cutLeft = 60;
    cut->cutRight = 61;
    auto* model = new partition_model(cut.get(), 2);
    return phoenix::partition::ProductionModelRef(
        model,
        [cut = std::move(cut)](partition_model* model) { delete model; });
}

phoenix::FunctionDescriptor partition_function()
{
    using phoenix::PortDirection;
    phoenix::FunctionDescriptor function;
    function.id = "partition-integration";
    function.input_ports = {{"geometry", "geometry", PortDirection::input}};
    function.output_ports = {{"result", "geometry", PortDirection::output}};
    phoenix::InstructionDescriptor partition;
    partition.id = 1;
    partition.kind = "partition";
    partition.input_ports = {{"geometry", "geometry", PortDirection::input}};
    partition.output_ports = {{"result", "geometry", PortDirection::output}};
    partition.has_else_port = false;
    partition.consumes_geometry = true;
    phoenix::InstructionDescriptor output;
    output.id = 99;
    output.kind = "output";
    output.input_ports = {{"result", "geometry", PortDirection::input}};
    output.has_else_port = false;
    function.instructions = {partition, output};
    function.edges = {{1, "result", 99, "result"}};
    function.output_node_id = 99;
    return function;
}

phoenix::partition::InstructionConfig config()
{
    phoenix::partition::InstructionConfig result;
    result.model_factory = one_cut_model;
    result.base_segment_labels = {
        {0, phoenix::LabelId{30}}, {1, phoenix::LabelId{32}}};
    return result;
}

bool execution_and_publication()
{
    phoenix::InstructionRegistry registry;
    registry.register_handler("partition",
        phoenix::partition::make_instruction_handler(config()));
    const auto function = partition_function();
    const auto source = rectangle();
    phoenix::GeometryPublicationLedger ledger;
    phoenix::RunElementIdAllocator ids(1000);
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(
        source, "source", "actor:root")}};
    request.context = {function.id, {"root"}, 17};
    request.publication_ledger = &ledger;
    request.element_ids = &ids;
    request.kernel_version = "partition-production-exact-v1";
    request.adapter_version = "partition-canonical-v1";
    const auto result = phoenix::FunctionExecutor{registry}.run(request);
    const auto assembled = ledger.assemble_actor("actor:root");
    const bool ok = result.status == phoenix::FunctionExecutionStatus::completed
        && result.failures.empty() && assembled
        && assembled->faces().size() == 2
        && ledger.is_consumed({"actor:root", phoenix::FaceId{40}});
    if (!ok) {
        const auto* collection = result.outputs.empty()
            ? nullptr : result.outputs.front().value.as_geometry_collection();
        std::cout << "partition publication detail: status="
                  << phoenix::to_string(result.status)
                  << " failures=" << result.failures.size()
                  << " assembled=" << static_cast<bool>(assembled)
                  << " faces=" << (assembled ? assembled->faces().size() : 0)
                  << " consumed="
                  << ledger.is_consumed({"actor:root", phoenix::FaceId{40}})
                  << " output_items=" << (collection ? collection->contributions.size() : 0)
                  << " output_faces=" << (collection && !collection->contributions.empty()
                      ? collection->contributions.front().geometry->faces().size() : 0)
                  << '\n';
        if (!result.failures.empty())
            std::cout << "partition publication failure: "
                      << result.failures.front().message << '\n';
    }
    return ok;
}

bool cache_skips_kernel()
{
    int calls = 0;
    auto linked = config();
    linked.model_factory = [&calls] { ++calls; return one_cut_model(); };
    phoenix::InstructionRegistry registry;
    registry.register_handler("partition",
        phoenix::partition::make_instruction_handler(linked));
    const auto function = partition_function();
    phoenix::MemoryCacheStore cache;
    phoenix::RunElementIdAllocator ids(2000);
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(
        rectangle(), "source", "partition-cache-owner")}};
    request.context = {function.id, {"root"}, 23};
    request.element_ids = &ids;
    request.cache_writer = &cache;
    request.kernel_version = "partition-production-exact-v1";
    request.adapter_version = "partition-canonical-v1";
    const phoenix::FunctionExecutor executor{registry};
    const auto first = executor.run(request);
    request.cache_writer = nullptr;
    request.cache_store = &cache;
    const auto second = executor.run(request);
    return first.status == phoenix::FunctionExecutionStatus::completed
        && second.status == phoenix::FunctionExecutionStatus::completed
        && calls == 1;
}

bool failure_preserves_item_context()
{
    auto invalid = config();
    invalid.model_factory = [] { return phoenix::partition::ProductionModelRef{}; };
    auto handler = phoenix::partition::make_instruction_handler(std::move(invalid));
    phoenix::RunElementIdAllocator ids(3000);
    phoenix::InstructionExecutionFrame frame;
    frame.inputs.node_id = 7;
    frame.inputs.promised_inputs = {{"geometry", phoenix::RuntimeValue::geometry(
        rectangle(), "source", "owner")}};
    frame.context.global_seed = 31;
    frame.element_ids = &ids;
    const auto result = handler(frame);
    return result.failures.size() == 1
        && result.failures[0].item_key == 0
        && result.geometry_effects.size() == 1
        && !result.geometry_effects[0].succeeded
        && result.geometry_effects[0].consumed_faces.empty();
}

bool fanout_is_deterministic_across_worker_requests()
{
    phoenix::InstructionRegistry registry;
    registry.register_handler("partition",
        phoenix::partition::make_instruction_handler(config()));
    const auto function = partition_function();
    auto run = [&](std::size_t workers, std::int64_t first_id) {
        phoenix::GeometryPublicationLedger ledger;
        phoenix::RunElementIdAllocator ids(first_id);
        phoenix::FunctionExecutionRequest request;
        request.function = &function;
        request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(
            two_rectangles(), "source", "actor:root")}};
        request.context = {function.id, {"root"}, 47};
        request.options.worker_count = workers;
        request.publication_ledger = &ledger;
        request.element_ids = &ids;
        request.kernel_version = "partition-production-exact-v1";
        request.adapter_version = "partition-canonical-v1";
        const auto execution = phoenix::FunctionExecutor{registry}.run(request);
        return std::pair{execution, ledger.assemble_actor("actor:root")};
    };
    const auto serial = run(1, 4000);
    const auto worker = run(4, 4000);
    if (serial.first.status != phoenix::FunctionExecutionStatus::completed
        || worker.first.status != phoenix::FunctionExecutionStatus::completed
        || !serial.first.failures.empty() || !worker.first.failures.empty()
        || !serial.second || !worker.second
        || serial.second->faces().size() != 4
        || worker.second->faces().size() != 4) return false;
    const auto& serial_faces = serial.second->faces();
    const auto& worker_faces = worker.second->faces();
    for (std::size_t index = 0; index < serial_faces.size(); ++index) {
        if (serial_faces[index].id != worker_faces[index].id
            || serial_faces[index].label != worker_faces[index].label) return false;
    }
    return true;
}

} // namespace

int main()
{
    const bool publication = execution_and_publication();
    const bool cache = cache_skips_kernel();
    const bool failure = failure_preserves_item_context();
    const bool fanout = fanout_is_deterministic_across_worker_requests();
    std::cout << "partition instruction publication: " << publication << '\n'
              << "partition instruction cache: " << cache << '\n'
              << "partition instruction failure context: " << failure << '\n'
              << "partition instruction deterministic fan-out: " << fanout << '\n';
    return publication && cache && failure && fanout ? EXIT_SUCCESS : EXIT_FAILURE;
}
