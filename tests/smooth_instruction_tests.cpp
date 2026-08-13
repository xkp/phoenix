#include "phoenix/smooth/instruction.hpp"
#include "phoenix/partial_run_apply.hpp"
#include "phoenix/partial_run_scope.hpp"

#include <iostream>

namespace {

phoenix::CanonicalGeometryRef quad()
{
    using namespace phoenix;
    return CanonicalGeometry::create({
        {{0, 0, 0}, VertexId{1}, 0}, {{2, 0, 0}, VertexId{2}, 1},
        {{2, 2, 0}, VertexId{3}, 2}, {{0, 2, 0}, VertexId{4}, 3}}, {
        {0, 0, 1, 3, invalid_geometry_index, 0, HalfedgeId{10}, EdgeId{20}, LabelId{31}},
        {1, 0, 2, 0, invalid_geometry_index, 1, HalfedgeId{11}, EdgeId{21}, LabelId{32}},
        {2, 0, 3, 1, invalid_geometry_index, 2, HalfedgeId{12}, EdgeId{22}, LabelId{33}},
        {3, 0, 0, 2, invalid_geometry_index, 3, HalfedgeId{13}, EdgeId{23}, LabelId{34}}},
        {{0, FaceId{40}, LabelId{77}}});
}

phoenix::FunctionDescriptor smooth_function()
{
    using namespace phoenix;
    FunctionDescriptor function;
    function.id = "smooth-integration";
    function.input_ports = {{"geometry", "geometry", PortDirection::input}};
    function.output_ports = {{"result", "geometry", PortDirection::output}};
    InstructionDescriptor smooth;
    smooth.id = 1;
    smooth.kind = "smooth";
    smooth.input_ports = {{"geometry", "geometry", PortDirection::input}};
    smooth.output_ports = {{"result", "geometry", PortDirection::output}};
    smooth.consumes_geometry = true;
    InstructionDescriptor output;
    output.id = 99;
    output.kind = "output";
    output.input_ports = {{"result", "geometry", PortDirection::input}};
    function.instructions = {smooth, output};
    function.edges = {{1, "result", 99, "result"}};
    function.output_node_id = 99;
    return function;
}

bool successful_publication_and_cache()
{
    using namespace phoenix;
    smooth::InstructionConfig config;
    config.options.max_refinement_level = 1;
    const auto handler = smooth::make_instruction_handler(config);
    std::size_t invocations = 0;
    InstructionRegistry registry;
    registry.register_handler("smooth", [&invocations, handler](const auto& frame) {
        ++invocations;
        return handler(frame);
    });
    const auto function = smooth_function();
    const auto source = quad();
    GeometryPublicationLedger ledger;
    MemoryCacheStore cache;
    RunElementIdAllocator ids(1000);
    FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", RuntimeValue::geometry(
        source, "source", ActorId{"actor:smooth"})}};
    request.context = {function.id, {"smooth"}, 11};
    request.publication_ledger = &ledger;
    request.element_ids = &ids;
    request.cache_store = &cache;
    request.cache_writer = &cache;
    request.kernel_version = "smooth-opensubdiv-3.5-v1";
    request.adapter_version = "smooth-canonical-3d-v1";
    const FunctionExecutor executor{registry};
    const auto first = executor.run(request);
    const auto geometry = ledger.assemble_actor("actor:smooth");
    const auto second = executor.run(request);
    const auto replayed = ledger.assemble_actor("actor:smooth");
    return first.status == FunctionExecutionStatus::completed && first.failures.empty()
        && geometry && geometry->faces().size() == 4
        && ledger.is_consumed({"actor:smooth", FaceId{40}})
        && second.status == FunctionExecutionStatus::completed && invocations == 1
        && replayed && replayed->fingerprint() == geometry->fingerprint();
}

bool failure_is_transactional()
{
    using namespace phoenix;
    smooth::InstructionConfig config;
    config.options.scheme = smooth::SubdivisionScheme::loop;
    const auto handler = smooth::make_instruction_handler(config);
    InstructionExecutionFrame frame;
    frame.inputs.node_id = 1;
    frame.inputs.promised_inputs = {{"geometry", RuntimeValue::geometry(
        quad(), "source", ActorId{"actor:smooth-failure"})}};
    RunElementIdAllocator ids(2000);
    frame.element_ids = &ids;
    const auto result = handler(frame);
    return result.geometry_effects.size() == 1
        && !result.geometry_effects[0].succeeded
        && !result.geometry_effects[0].generated_geometry
        && result.geometry_effects[0].consumed_faces.empty()
        && result.failures.size() == 1;
}

bool partial_rerun_restores_source()
{
    using namespace phoenix;
    smooth::InstructionConfig config;
    config.options.max_refinement_level = 1;
    const auto handler = smooth::make_instruction_handler(config);
    std::size_t runs = 0;
    InstructionRegistry registry;
    registry.register_handler("smooth", [&runs, handler](const auto& frame) {
        ++runs;
        return handler(frame);
    });
    auto function = smooth_function();
    function.id = "smooth-partial-e2e";
    function.generates_actor = true;
    const auto source = quad();
    MemoryCacheStore cache;
    GeometryPublicationLedger ledger;
    RunElementIdAllocator ids(5000);
    FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", RuntimeValue::geometry(
        source, "source", ActorId{"actor:smooth-partial"})}};
    request.context = {function.id, {"smooth-partial"}, 23};
    request.publication_ledger = &ledger;
    request.element_ids = &ids;
    request.cache_store = &cache;
    request.cache_writer = &cache;
    request.label_registry_fingerprint = 9876;
    request.kernel_version = "smooth-opensubdiv-3.5-v1";
    request.adapter_version = "smooth-canonical-3d-v1";
    const FunctionExecutor executor{registry};
    const auto full = executor.run(request);
    if (!full.actor || !full.actor->geometry || !full.actor->geometry->geometry)
        return false;
    const auto refined_fingerprint = full.actor->geometry->geometry->fingerprint();
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
    plan_request.actor_id = "actor:smooth-partial";
    plan_request.cache_store = &cache;
    const auto plan = PartialRerunPlanner{}.plan(plan_request);
    PartialRerunScopeRequest scope_request;
    scope_request.plan = &plan;
    scope_request.function = &function;
    scope_request.inputs = request.inputs;
    scope_request.context = request.context;
    scope_request.actor_id = "actor:smooth-partial";
    scope_request.cache_store = &cache;
    scope_request.cache_writer = &cache;
    scope_request.publication_ledger = &ledger;
    scope_request.element_ids = &ids;
    scope_request.label_registry_fingerprint = request.label_registry_fingerprint;
    scope_request.kernel_version = request.kernel_version;
    scope_request.adapter_version = request.adapter_version;
    const auto scope = PartialRerunScopeResolver{}.resolve(scope_request);
    if (!scope.execution_request) return false;
    const auto replay = PartialRerunApplier{}.apply(
        {&scene, &plan, &cache, &executor, &*scope.execution_request});
    const auto replayed = scene.root.children[0].geometry
        ? scene.root.children[0].geometry->geometry : nullptr;
    if (replay.status != PartialRerunApplyStatus::applied_rerun_actor_subtree
        || runs != 1 || !replayed || replayed->fingerprint() != refined_fingerprint)
        return false;

    InstructionRegistry failing_registry;
    failing_registry.register_handler("smooth", [](const auto& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        result.geometry_effects.push_back({0, false, nullptr, {}, "changed smooth failure"});
        return result;
    });
    scope_request.kernel_version = "smooth-opensubdiv-3.5-v2";
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
        && !ledger.is_consumed({"actor:smooth-partial", FaceId{40}});
}

} // namespace

int main()
{
    const bool publication = successful_publication_and_cache();
    const bool transactional = failure_is_transactional();
    const bool partial = partial_rerun_restores_source();
    std::cout << "smooth publication and cache replay: " << publication << '\n'
              << "smooth transactional failure: " << transactional << '\n'
              << "smooth partial-rerun restoration: " << partial << '\n';
    return publication && transactional && partial ? 0 : 1;
}
