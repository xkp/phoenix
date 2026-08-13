#include "phoenix/inset/production_adapter.hpp"
#include "phoenix/inset/instruction.hpp"
#include "phoenix/partial_run_apply.hpp"
#include "phoenix/partial_run_scope.hpp"

#include <cstdlib>
#include <iostream>
#include <set>

namespace {

phoenix::CanonicalGeometryRef tilted_rectangle()
{
    using namespace phoenix;
    return CanonicalGeometry::create({
        {{1, 2, 3}, VertexId{1}, 0}, {{7, 2, 3}, VertexId{2}, 1},
        {{7, 6, 7}, VertexId{3}, 2}, {{1, 6, 7}, VertexId{4}, 3}}, {
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

phoenix::CanonicalGeometryRef tilted_concave_face()
{
    using namespace phoenix;
    const std::vector<Point3d> points{
        {0, 0, 1}, {6, 0, 1}, {6, 2, 3},
        {2, 2, 3}, {2, 6, 7}, {0, 6, 7}};
    std::vector<RuntimeVertex> vertices;
    std::vector<RuntimeHalfedge> halfedges;
    for (GeometryIndex index = 0; index < points.size(); ++index) {
        vertices.push_back({points[index], VertexId{100 + index}, index});
        halfedges.push_back({index, 0,
            static_cast<GeometryIndex>((index + 1) % points.size()),
            static_cast<GeometryIndex>((index + points.size() - 1) % points.size()),
            invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{200 + index}, EdgeId{300 + index},
            LabelId{70 + static_cast<std::int32_t>(index)}});
    }
    return CanonicalGeometry::create(std::move(vertices), std::move(halfedges),
        {{0, FaceId{400}, LabelId{401}}});
}

phoenix::FunctionDescriptor inset_function()
{
    using phoenix::PortDirection;
    phoenix::FunctionDescriptor function;
    function.id = "inset-integration";
    function.input_ports = {{"geometry", "geometry", PortDirection::input}};
    function.output_ports = {{"result", "geometry", PortDirection::output}};
    phoenix::InstructionDescriptor inset;
    inset.id = 1;
    inset.kind = "inset";
    inset.input_ports = {{"geometry", "geometry", PortDirection::input}};
    inset.output_ports = {{"result", "geometry", PortDirection::output}};
    inset.consumes_geometry = true;
    phoenix::InstructionDescriptor output;
    output.id = 99;
    output.kind = "output";
    output.input_ports = {{"result", "geometry", PortDirection::input}};
    function.instructions = {inset, output};
    function.edges = {{1, "result", 99, "result"}};
    function.output_node_id = 99;
    return function;
}

bool partial_rerun_restores_source(const phoenix::inset::InstructionConfig& config)
{
    using namespace phoenix;
    std::size_t runs = 0;
    const auto inset_handler = inset::make_instruction_handler(config);
    InstructionRegistry registry;
    registry.register_handler("inset", [&runs, inset_handler](const auto& frame) {
        ++runs;
        return inset_handler(frame);
    });
    auto function = inset_function();
    function.id = "inset-partial-e2e";
    function.generates_actor = true;
    const auto source = tilted_rectangle();
    MemoryCacheStore cache;
    GeometryPublicationLedger ledger;
    RunElementIdAllocator ids(6000);
    FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", RuntimeValue::geometry(
        source, "source", "actor:inset-partial")}};
    request.context = {function.id, {"inset-partial"}, 23};
    request.publication_ledger = &ledger;
    request.element_ids = &ids;
    request.cache_store = &cache;
    request.cache_writer = &cache;
    request.label_registry_fingerprint = 4321;
    request.kernel_version = "inset-production-exact-v1";
    request.adapter_version = "inset-canonical-3d-v1";
    const FunctionExecutor executor{registry};
    const auto full = executor.run(request);
    if (!full.actor || !full.actor->geometry || !full.actor->geometry->geometry)
        return false;
    const auto full_fingerprint = full.actor->geometry->geometry->fingerprint();
    SceneRoot scene;
    scene.root.id = "scene-root";
    scene.root.children.push_back(*full.actor);
    const auto identity = CacheIdentityBuilder{}.identity({
        &function, request.context.call_path, request.inputs,
        request.context.global_seed, request.label_registry_fingerprint,
        request.kernel_version, request.adapter_version,
        request.repair_policy_version});
    PartialRerunRequest plan_request;
    plan_request.function = &function;
    plan_request.changed_instructions = {1};
    plan_request.cache_identity = identity;
    plan_request.actor_id = "actor:inset-partial";
    plan_request.cache_store = &cache;
    const auto plan = PartialRerunPlanner{}.plan(plan_request);
    if (plan.actor_subtree_cache_hit || plan.instructions.empty()) return false;
    PartialRerunScopeRequest scope_request;
    scope_request.plan = &plan;
    scope_request.function = &function;
    scope_request.inputs = request.inputs;
    scope_request.context = request.context;
    scope_request.actor_id = "actor:inset-partial";
    scope_request.cache_store = &cache;
    scope_request.cache_writer = &cache;
    scope_request.publication_ledger = &ledger;
    scope_request.element_ids = &ids;
    scope_request.label_registry_fingerprint = request.label_registry_fingerprint;
    scope_request.kernel_version = request.kernel_version;
    scope_request.adapter_version = request.adapter_version;
    const auto scope = PartialRerunScopeResolver{}.resolve(scope_request);
    if (!scope.execution_request) return false;
    const auto applied = PartialRerunApplier{}.apply(
        {&scene, &plan, &cache, &executor, &*scope.execution_request});
    const auto replayed = scene.root.children[0].geometry
        ? scene.root.children[0].geometry->geometry : nullptr;
    if (applied.status != PartialRerunApplyStatus::applied_rerun_actor_subtree
        || runs != 1 || !replayed
        || replayed->fingerprint() != full_fingerprint) return false;

    InstructionRegistry failing_registry;
    failing_registry.register_handler("inset", [](const auto& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        result.geometry_effects.push_back(
            {0, false, nullptr, {}, "changed inset failure"});
        return result;
    });
    scope_request.kernel_version = "inset-production-exact-v2";
    plan_request.cache_identity = CacheIdentityBuilder{}.identity({
        &function, request.context.call_path, request.inputs,
        request.context.global_seed, request.label_registry_fingerprint,
        scope_request.kernel_version, request.adapter_version,
        request.repair_policy_version});
    const auto changed_plan = PartialRerunPlanner{}.plan(plan_request);
    scope_request.plan = &changed_plan;
    const auto changed_scope = PartialRerunScopeResolver{}.resolve(scope_request);
    if (!changed_scope.execution_request) return false;
    const FunctionExecutor failing_executor{failing_registry};
    const auto changed = PartialRerunApplier{}.apply(
        {&scene, &changed_plan, &cache, &failing_executor,
            &*changed_scope.execution_request});
    const auto restored = scene.root.children[0].geometry
        ? scene.root.children[0].geometry->geometry : nullptr;
    return changed.status == PartialRerunApplyStatus::applied_rerun_actor_subtree
        && restored && restored->faces().size() == 1
        && restored->faces()[0].id == FaceId{40}
        && !ledger.is_consumed({"actor:inset-partial", FaceId{40}});
}

} // namespace

int main()
{
    using namespace phoenix;
    const auto source = tilted_rectangle();
    inset::ProductionInsetRequest request;
    request.source = {source, 0, FaceId{40}};
    request.amount = 1.0;
    request.labels.result_face = LabelId{60};
    request.labels.side_face = LabelId{61};
    request.labels.result_edge = LabelId{62};
    request.labels.left_edge = LabelId{63};
    request.labels.right_edge = LabelId{64};
    request.labels.top_edge = LabelId{65};
    request.labels.bottom_edge = LabelId{66};
    RunElementIdAllocator ids(1000);
    const auto result = inset::run_production_inset(request, ids);
    std::set<std::int32_t> face_labels;
    std::set<std::int32_t> edge_labels;
    bool planar = result.geometry != nullptr;
    if (result.geometry) {
        for (const auto& face : result.geometry->faces())
            face_labels.insert(face.label.value());
        for (const auto& edge : result.geometry->halfedges())
            edge_labels.insert(edge.label.value());
        for (const auto& vertex : result.geometry->vertices())
            planar = planar && std::abs((vertex.point.y - 2.0)
                - (vertex.point.z - 3.0)) < 1e-9;
    }
    const bool canonical = result.success()
        && result.consumed_face_id == FaceId{40}
        && result.geometry->faces().size() == 5
        && result.geometry->vertices().size() == 8 && planar;
    const bool labels = face_labels == std::set<std::int32_t>{60, 61}
        && edge_labels.count(62) == 1 && edge_labels.count(63) == 1
        && edge_labels.count(64) == 1 && edge_labels.count(65) == 1
        && edge_labels.count(66) == 1;

    GeometryPublicationLedger ledger;
    const ActorId actor{"actor:root"};
    ledger.set_actor_source(actor, source);
    const auto commit = ledger.replace_scope(
        {FunctionId{"inset-test"}, {}, NodeId{1}}, actor, true,
        {result.publication_effect(0, actor)});
    const auto assembled = ledger.assemble_actor(actor);
    const bool replacement = assembled && assembled->faces().size() == 5
        && ledger.is_consumed({actor, FaceId{40}})
        && commit.diagnostics.empty();

    inset::ProductionInsetRequest invalid = request;
    invalid.amount = 0.0;
    RunElementIdAllocator invalid_ids(2000);
    const auto failed = inset::run_production_inset(invalid, invalid_ids);
    const auto failed_effect = failed.publication_effect(0, actor);
    const bool failure = !failed.success() && !failed.error.empty()
        && failed_effect.consumed_faces.empty();

    inset::InstructionConfig instruction_config;
    instruction_config.amount = 1.0;
    instruction_config.labels = request.labels;
    auto handler = inset::make_instruction_handler(instruction_config);
    RunElementIdAllocator instruction_ids(3000);
    InstructionExecutionFrame frame;
    frame.inputs.node_id = 7;
    frame.inputs.promised_inputs = {{"geometry", RuntimeValue::geometry(
        source, "source", actor)}};
    frame.element_ids = &instruction_ids;
    const auto instruction_result = handler(frame);
    const auto* output = instruction_result.produced_outputs.empty() ? nullptr
        : instruction_result.produced_outputs.front().value.as_geometry_collection();
    const bool instruction = instruction_result.failures.empty()
        && instruction_result.geometry_effects.size() == 1
        && instruction_result.geometry_effects[0].consumed_faces.size() == 1
        && output && output->contributions.size() == 1
        && output->contributions[0].geometry->faces().size() == 5;

    inset::ProductionInsetRequest concave_request = request;
    concave_request.source = {tilted_concave_face(), 0, FaceId{400}};
    concave_request.amount = 0.5;
    RunElementIdAllocator concave_ids(3500);
    const auto concave_result = inset::run_production_inset(
        concave_request, concave_ids);
    bool concave_planar = concave_result.success();
    if (concave_result.geometry) {
        for (const auto& vertex : concave_result.geometry->vertices())
            concave_planar = concave_planar
                && std::abs(vertex.point.z - vertex.point.y - 1.0) < 1e-9;
    }
    const bool concave = concave_result.success() && concave_planar
        && concave_result.consumed_face_id == FaceId{400}
        && concave_result.geometry->faces().size() == 7;

    inset::ProductionInsetRequest inherited_request = request;
    inherited_request.labels = {};
    RunElementIdAllocator inherited_ids(3750);
    const auto inherited_result = inset::run_production_inset(
        inherited_request, inherited_ids);
    std::set<std::int32_t> inherited_face_labels;
    std::set<std::int32_t> inherited_edge_labels;
    if (inherited_result.geometry) {
        for (const auto& face : inherited_result.geometry->faces())
            inherited_face_labels.insert(face.label.value());
        for (const auto& edge : inherited_result.geometry->halfedges())
            inherited_edge_labels.insert(edge.label.value());
    }
    const bool inherited_labels = inherited_result.success()
        && inherited_face_labels == std::set<std::int32_t>{30, 31, 32, 33, 41}
        && inherited_edge_labels.count(30) == 1
        && inherited_edge_labels.count(31) == 1
        && inherited_edge_labels.count(32) == 1
        && inherited_edge_labels.count(33) == 1
        && inherited_edge_labels.count(-1) == 1;

    InstructionRegistry registry;
    registry.register_handler("inset", inset::make_instruction_handler(
        instruction_config));
    const auto function = inset_function();
    GeometryPublicationLedger execution_ledger;
    RunElementIdAllocator execution_ids(4000);
    FunctionExecutionRequest execution_request;
    execution_request.function = &function;
    execution_request.inputs = {{"geometry", RuntimeValue::geometry(
        source, "source", actor)}};
    execution_request.context = {function.id, {"root"}, 17};
    execution_request.publication_ledger = &execution_ledger;
    execution_request.element_ids = &execution_ids;
    execution_request.kernel_version = "inset-production-exact-v1";
    execution_request.adapter_version = "inset-canonical-3d-v1";
    const auto execution_result = FunctionExecutor{registry}.run(execution_request);
    const auto execution_geometry = execution_ledger.assemble_actor(actor);
    const bool end_to_end =
        execution_result.status == FunctionExecutionStatus::completed
        && execution_result.failures.empty() && execution_geometry
        && execution_geometry->faces().size() == 5
        && execution_ledger.is_consumed({actor, FaceId{40}});

    std::size_t cache_invocations = 0;
    const auto cached_handler = inset::make_instruction_handler(instruction_config);
    InstructionRegistry cached_registry;
    cached_registry.register_handler("inset",
        [&cache_invocations, cached_handler](const InstructionExecutionFrame& cached_frame) {
            ++cache_invocations;
            return cached_handler(cached_frame);
        });
    MemoryCacheStore cache;
    GeometryPublicationLedger cache_ledger;
    RunElementIdAllocator cache_ids(5000);
    FunctionExecutionRequest cache_request = execution_request;
    cache_request.inputs = {{"geometry", RuntimeValue::geometry(
        source, "source", ActorId{"actor:inset-cache"})}};
    cache_request.context.call_path = {"inset-cache"};
    cache_request.publication_ledger = &cache_ledger;
    cache_request.element_ids = &cache_ids;
    cache_request.cache_store = &cache;
    cache_request.cache_writer = &cache;
    const FunctionExecutor cached_executor{cached_registry};
    const auto uncached_execution = cached_executor.run(cache_request);
    const auto first_cached_geometry = cache_ledger.assemble_actor("actor:inset-cache");
    const auto cached_execution = cached_executor.run(cache_request);
    const auto replayed_geometry = cache_ledger.assemble_actor("actor:inset-cache");
    const bool cache_replay =
        uncached_execution.status == FunctionExecutionStatus::completed
        && cached_execution.status == FunctionExecutionStatus::completed
        && cache_invocations == 1 && first_cached_geometry && replayed_geometry
        && first_cached_geometry->fingerprint() == replayed_geometry->fingerprint()
        && cache_ledger.is_consumed({"actor:inset-cache", FaceId{40}});
    const bool partial_rerun = partial_rerun_restores_source(instruction_config);
    if (!end_to_end) {
        std::cout << "inset executor detail: status="
                  << to_string(execution_result.status)
                  << " failures=" << execution_result.failures.size()
                  << " assembled=" << static_cast<bool>(execution_geometry)
                  << " faces=" << (execution_geometry
                      ? execution_geometry->faces().size() : 0)
                  << " consumed="
                  << execution_ledger.is_consumed({actor, FaceId{40}})
                  << " outputs=" << execution_result.outputs.size() << '\n';
        if (!execution_result.failures.empty())
            std::cout << "inset executor failure: "
                      << execution_result.failures.front().message << '\n';
    }

    std::cout << "production inset canonical 3D boundary: " << canonical << '\n'
              << "production inset directed labels: " << labels << '\n'
              << "production inset consumes replacement: " << replacement << '\n'
              << "production inset failure preserves source: " << failure << '\n';
    std::cout << "production inset 3D instruction boundary: " << instruction << '\n'
              << "production inset concave tilted face: " << concave << '\n'
              << "production inset default label inheritance: "
              << inherited_labels << '\n'
              << "production inset executor publication: " << end_to_end << '\n'
              << "production inset cache replay: " << cache_replay << '\n'
              << "production inset partial rerun restoration: "
              << partial_rerun << '\n';
    if (!concave) std::cout << "concave detail: success=" << concave_result.success()
        << " faces=" << (concave_result.geometry
            ? concave_result.geometry->faces().size() : 0)
        << " vertices=" << (concave_result.geometry
            ? concave_result.geometry->vertices().size() : 0)
        << " error=" << concave_result.error << '\n';
    if (!canonical || !labels) std::cout << "detail: " << result.error << '\n';
    return canonical && labels && replacement && failure && instruction && concave
        && inherited_labels && end_to_end && cache_replay
        && partial_rerun
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
