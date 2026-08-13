#include "phoenix/merge/production_adapter.hpp"
#include "phoenix/merge/instruction.hpp"
#include "phoenix/partial_run_apply.hpp"
#include "phoenix/partial_run_scope.hpp"

#include <cstdlib>
#include <iostream>

namespace {

phoenix::CanonicalGeometryRef square_triangles()
{
    using namespace phoenix;
    std::vector<RuntimeVertex> vertices{
        {{0, 0, 0}, VertexId{1}, 0}, {{1, 0, 0}, VertexId{2}, 1},
        {{1, 1, 0}, VertexId{3}, 2}, {{0, 1, 0}, VertexId{4}, 4}};
    std::vector<RuntimeHalfedge> halfedges{
        {0, 0, 1, 2, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{10}, EdgeId{20}, LabelId{30}},
        {1, 0, 2, 0, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{11}, EdgeId{21}, LabelId{31}},
        {2, 0, 0, 1, 3, invalid_geometry_index,
            HalfedgeId{12}, EdgeId{22}, LabelId{32}},
        {0, 1, 4, 5, 2, invalid_geometry_index,
            HalfedgeId{13}, EdgeId{22}, LabelId{33}},
        {2, 1, 5, 3, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{14}, EdgeId{23}, LabelId{34}},
        {3, 1, 3, 4, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{15}, EdgeId{24}, LabelId{35}}};
    return CanonicalGeometry::create(std::move(vertices), std::move(halfedges),
        {{0, FaceId{40}, LabelId{77}}, {3, FaceId{41}, LabelId{77}}});
}

phoenix::FunctionDescriptor merge_function()
{
    using phoenix::PortDirection;
    phoenix::FunctionDescriptor function;
    function.id = "merge-integration";
    function.input_ports = {{"geometry", "geometry", PortDirection::input}};
    function.output_ports = {{"result", "geometry", PortDirection::output}};
    phoenix::InstructionDescriptor merge;
    merge.id = 1;
    merge.kind = "merge";
    merge.input_ports = {{"geometry", "geometry", PortDirection::input}};
    merge.output_ports = {{"result", "geometry", PortDirection::output}};
    merge.consumes_geometry = true;
    phoenix::InstructionDescriptor output;
    output.id = 99;
    output.kind = "output";
    output.input_ports = {{"result", "geometry", PortDirection::input}};
    function.instructions = {merge, output};
    function.edges = {{1, "result", 99, "result"}};
    function.output_node_id = 99;
    return function;
}

bool partial_rerun_restores_source(const phoenix::merge::InstructionConfig& config)
{
    using namespace phoenix;
    std::size_t runs = 0;
    const auto handler = merge::make_instruction_handler(config);
    InstructionRegistry registry;
    registry.register_handler("merge", [&runs, handler](const auto& frame) {
        ++runs;
        return handler(frame);
    });
    auto function = merge_function();
    function.id = "merge-partial-e2e";
    function.generates_actor = true;
    const auto source = square_triangles();
    MemoryCacheStore cache;
    GeometryPublicationLedger ledger;
    RunElementIdAllocator ids(6000);
    FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", RuntimeValue::geometry(
        source, "source", "actor:merge-partial")}};
    request.context = {function.id, {"merge-partial"}, 23};
    request.publication_ledger = &ledger;
    request.element_ids = &ids;
    request.cache_store = &cache;
    request.cache_writer = &cache;
    request.label_registry_fingerprint = 4321;
    request.kernel_version = "merge-production-exact-v1";
    request.adapter_version = "merge-canonical-3d-v1";
    const FunctionExecutor executor{registry};
    const auto full = executor.run(request);
    if (!full.actor || !full.actor->geometry || !full.actor->geometry->geometry)
        return false;
    const auto fingerprint = full.actor->geometry->geometry->fingerprint();
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
    plan_request.actor_id = "actor:merge-partial";
    plan_request.cache_store = &cache;
    const auto plan = PartialRerunPlanner{}.plan(plan_request);
    PartialRerunScopeRequest scope_request;
    scope_request.plan = &plan;
    scope_request.function = &function;
    scope_request.inputs = request.inputs;
    scope_request.context = request.context;
    scope_request.actor_id = "actor:merge-partial";
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
        || runs != 1 || !replayed || replayed->fingerprint() != fingerprint) return false;

    InstructionRegistry failing_registry;
    failing_registry.register_handler("merge", [](const auto& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        result.geometry_effects.push_back({0, false, nullptr, {}, "changed merge failure"});
        return result;
    });
    scope_request.kernel_version = "merge-production-exact-v2";
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
        && restored && restored->faces().size() == 2
        && !ledger.is_consumed({"actor:merge-partial", FaceId{40}})
        && !ledger.is_consumed({"actor:merge-partial", FaceId{41}});
}

} // namespace

int main()
{
    using namespace phoenix;
    const auto source = square_triangles();
    merge::ProductionMergeRequest request;
    request.sources = {
        {{source, 0, FaceId{40}}, "actor:merge"},
        {{source, 1, FaceId{41}}, "actor:merge"}};
    request.options.merge_borders = true;
    request.options.join_vertices = true;
    request.options.merge_faces = true;
    request.options.merge_faces_labels = true;
    request.options.join_collinear = true;
    RunElementIdAllocator ids(1000);
    const auto result = merge::run_production_merge(request, ids);
    const auto effect = result.publication_effect(0);
    const bool success = result.success() && result.geometry->faces().size() == 1
        && result.geometry->vertices().size() == 4
        && effect.succeeded && effect.consumed_faces.size() == 2;

    merge::ProductionMergeRequest rejected_request = request;
    rejected_request.options = {};
    const auto rejected = merge::run_production_merge(rejected_request, ids);
    const auto rejected_effect = rejected.publication_effect(1);
    const bool transactional_failure = !rejected.success()
        && !rejected_effect.succeeded && rejected_effect.generated_geometry == nullptr
        && rejected_effect.consumed_faces.empty()
        && source->faces().size() == 2;

    merge::InstructionConfig config;
    config.options = request.options;
    std::size_t invocations = 0;
    const auto handler = merge::make_instruction_handler(config);
    InstructionRegistry registry;
    registry.register_handler("merge", [&invocations, handler](const auto& frame) {
        ++invocations;
        return handler(frame);
    });
    const auto function = merge_function();
    GeometryPublicationLedger ledger;
    MemoryCacheStore cache;
    RunElementIdAllocator execution_ids(3000);
    FunctionExecutionRequest execution;
    execution.function = &function;
    execution.inputs = {{"geometry", RuntimeValue::geometry(
        source, "source", ActorId{"actor:merge-e2e"})}};
    execution.context = {function.id, {"merge-e2e"}, 11};
    execution.publication_ledger = &ledger;
    execution.element_ids = &execution_ids;
    execution.cache_store = &cache;
    execution.cache_writer = &cache;
    execution.kernel_version = "merge-production-exact-v1";
    execution.adapter_version = "merge-canonical-3d-v1";
    const FunctionExecutor executor{registry};
    const auto first_execution = executor.run(execution);
    const auto first_geometry = ledger.assemble_actor("actor:merge-e2e");
    const auto second_execution = executor.run(execution);
    const auto replayed_geometry = ledger.assemble_actor("actor:merge-e2e");
    const bool end_to_end = first_execution.status == FunctionExecutionStatus::completed
        && first_execution.failures.empty() && first_geometry
        && first_geometry->faces().size() == 1
        && ledger.is_consumed({"actor:merge-e2e", FaceId{40}})
        && ledger.is_consumed({"actor:merge-e2e", FaceId{41}});
    const bool cache_replay = second_execution.status == FunctionExecutionStatus::completed
        && invocations == 1 && replayed_geometry
        && first_geometry->fingerprint() == replayed_geometry->fingerprint();
    const bool partial_rerun = partial_rerun_restores_source(config);

    std::cout << "production merge canonical adapter: " << success << '\n'
              << "production merge transactional publication failure: "
              << transactional_failure << '\n'
              << "production merge executor publication: " << end_to_end << '\n'
              << "production merge cache replay: " << cache_replay << '\n';
    std::cout << "production merge partial rerun restoration: "
              << partial_rerun << '\n';
    return success && transactional_failure && end_to_end && cache_replay
        && partial_rerun
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
