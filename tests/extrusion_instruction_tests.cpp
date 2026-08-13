#include "phoenix/extrusion/instruction.hpp"
#include "phoenix/partial_run_apply.hpp"
#include "phoenix/partial_run_scope.hpp"

#include <cstdlib>
#include <iostream>

namespace {

phoenix::CanonicalGeometryRef source_triangle()
{
    std::vector<phoenix::RuntimeVertex> vertices{
        {{0, 0, 0}, phoenix::VertexId{100}, 0},
        {{1, 0, 0}, phoenix::VertexId{101}, 1},
        {{0, 1, 0}, phoenix::VertexId{102}, 2},
    };
    std::vector<phoenix::RuntimeHalfedge> halfedges{
        {0, 0, 1, 2, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{110}, phoenix::EdgeId{120}, phoenix::LabelId{51}},
        {1, 0, 2, 0, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{111}, phoenix::EdgeId{121}, phoenix::LabelId{52}},
        {2, 0, 0, 1, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{112}, phoenix::EdgeId{122}, phoenix::LabelId{53}},
    };
    return phoenix::CanonicalGeometry::create(
        std::move(vertices), std::move(halfedges),
        {{0, phoenix::FaceId{130}, phoenix::LabelId{60}}});
}

phoenix::CanonicalGeometryRef source_two_triangles(bool degenerate_second = false)
{
    std::vector<phoenix::RuntimeVertex> vertices{
        {{0, 0, 0}, phoenix::VertexId{100}, 0},
        {{1, 0, 0}, phoenix::VertexId{101}, 1},
        {{0, 1, 0}, phoenix::VertexId{102}, 2},
        {{3, 0, 0}, phoenix::VertexId{200}, 3},
        {{4, 0, 0}, phoenix::VertexId{201}, 4},
        {{degenerate_second ? 5.0 : 3.0, degenerate_second ? 0.0 : 1.0, 0},
            phoenix::VertexId{202}, 5},
    };
    std::vector<phoenix::RuntimeHalfedge> halfedges{
        {0, 0, 1, 2, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{110}, phoenix::EdgeId{120}, phoenix::LabelId{51}},
        {1, 0, 2, 0, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{111}, phoenix::EdgeId{121}, phoenix::LabelId{52}},
        {2, 0, 0, 1, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{112}, phoenix::EdgeId{122}, phoenix::LabelId{53}},
        {3, 1, 4, 5, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{210}, phoenix::EdgeId{220}, phoenix::LabelId{54}},
        {4, 1, 5, 3, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{211}, phoenix::EdgeId{221}, phoenix::LabelId{55}},
        {5, 1, 3, 4, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            phoenix::HalfedgeId{212}, phoenix::EdgeId{222}, phoenix::LabelId{56}},
    };
    return phoenix::CanonicalGeometry::create(std::move(vertices), std::move(halfedges), {
        {0, phoenix::FaceId{130}, phoenix::LabelId{60}},
        {3, phoenix::FaceId{230}, phoenix::LabelId{61}},
    });
}

phoenix::FunctionDescriptor extrusion_function()
{
    using phoenix::PortDescriptor;
    using phoenix::PortDirection;
    phoenix::FunctionDescriptor function;
    function.id = "extrusion-integration";
    function.input_ports = {{"geometry", "geometry", PortDirection::input}};
    function.output_ports = {{"result", "geometry", PortDirection::output}};
    phoenix::InstructionDescriptor extrusion;
    extrusion.id = 1;
    extrusion.kind = "extrusion";
    extrusion.input_ports = {{"geometry", "geometry", PortDirection::input}};
    extrusion.output_ports = {{"result", "geometry", PortDirection::output}};
    extrusion.has_else_port = false;
    extrusion.consumes_geometry = true;
    phoenix::InstructionDescriptor output;
    output.id = 99;
    output.kind = "output";
    output.input_ports = {{"result", "geometry", PortDirection::input}};
    output.has_else_port = false;
    function.instructions = {extrusion, output};
    function.edges = {{1, "result", 99, "result"}};
    function.output_node_id = 99;
    return function;
}

phoenix::FunctionDescriptor extrusion_failure_function()
{
    using phoenix::PortDirection;
    phoenix::FunctionDescriptor function;
    function.id = "extrusion-failure";
    function.input_ports = {{"geometry", "geometry", PortDirection::input}};
    function.output_ports = {
        {"success", "geometry", PortDirection::output},
        {"fallback", "geometry", PortDirection::output},
    };
    phoenix::InstructionDescriptor extrusion;
    extrusion.id = 1;
    extrusion.kind = "extrusion";
    extrusion.input_ports = {{"geometry", "geometry", PortDirection::input}};
    extrusion.output_ports = {
        {"result", "geometry", PortDirection::output},
        {"else", "geometry", PortDirection::output},
    };
    extrusion.multiplexes_input = true;
    extrusion.consumes_geometry = true;
    phoenix::InstructionDescriptor fallback;
    fallback.id = 2;
    fallback.kind = "fallback";
    fallback.input_ports = {{"geometry", "geometry", PortDirection::input}};
    fallback.output_ports = {{"result", "geometry", PortDirection::output}};
    fallback.has_else_port = false;
    phoenix::InstructionDescriptor output;
    output.id = 99;
    output.kind = "output";
    output.input_ports = {
        {"success", "geometry", PortDirection::input},
        {"fallback", "geometry", PortDirection::input},
    };
    output.has_else_port = false;
    function.instructions = {extrusion, fallback, output};
    function.edges = {
        {1, "result", 99, "success"},
        {1, "else", 2, "geometry"},
        {2, "result", 99, "fallback"},
    };
    function.output_node_id = 99;
    return function;
}

phoenix::FunctionDescriptor extrusion_branches_function()
{
    using phoenix::PortDirection;
    phoenix::FunctionDescriptor function;
    function.id = "extrusion-branches";
    function.input_ports = {{"geometry", "geometry", PortDirection::input}};
    function.output_ports = {
        {"left", "geometry", PortDirection::output},
        {"right", "geometry", PortDirection::output},
    };
    auto branch = [](phoenix::NodeId id) {
        phoenix::InstructionDescriptor instruction;
        instruction.id = id;
        instruction.kind = "extrusion";
        instruction.input_ports = {{"geometry", "geometry", PortDirection::input}};
        instruction.output_ports = {{"result", "geometry", PortDirection::output}};
        instruction.has_else_port = false;
        instruction.consumes_geometry = true;
        return instruction;
    };
    phoenix::InstructionDescriptor output;
    output.id = 99;
    output.kind = "output";
    output.input_ports = {
        {"left", "geometry", PortDirection::input},
        {"right", "geometry", PortDirection::input},
    };
    output.has_else_port = false;
    function.instructions = {branch(1), branch(2), output};
    function.edges = {
        {1, "result", 99, "left"},
        {2, "result", 99, "right"},
    };
    function.output_node_id = 99;
    return function;
}

bool test_executor_consumes_and_publishes_replacement()
{
    phoenix::extrusion::ProfileSegment segment;
    segment.delta_y = 1;
    segment.face_label = phoenix::LabelId{70};
    segment.left_label = phoenix::LabelId{71};
    segment.bottom_label = phoenix::LabelId{72};
    segment.right_label = phoenix::LabelId{73};
    segment.top_label = phoenix::LabelId{74};
    segment.skirt_label = phoenix::LabelId{75};

    phoenix::extrusion::InstructionConfig config;
    config.profile = phoenix::extrusion::Profile::create({segment});
    config.bottom_label = phoenix::LabelId{80};
    config.right_label = phoenix::LabelId{81};
    config.top_label = phoenix::LabelId{82};
    config.left_label = phoenix::LabelId{83};
    config.skirt_label = phoenix::LabelId{84};
    config.cap_label = phoenix::LabelId{85};

    phoenix::InstructionRegistry registry;
    registry.register_handler("extrusion", phoenix::extrusion::make_instruction_handler(config));
    const auto function = extrusion_function();
    const auto source = source_triangle();
    phoenix::GeometryPublicationLedger ledger;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(source, "source", "actor:root")}};
    request.context.function_id = function.id;
    request.context.call_path = {"root"};
    request.publication_ledger = &ledger;

    const auto result = phoenix::FunctionExecutor{registry}.run(request);
    const auto final = ledger.assemble_actor("actor:root");
    if (result.status != phoenix::FunctionExecutionStatus::completed
        || !result.failures.empty() || !final || final->faces().size() != 4
        || !ledger.is_consumed({"actor:root", phoenix::FaceId{130}})) return false;
    for (const auto& face : final->faces())
        if (face.id == phoenix::FaceId{130}) return false;
    for (const auto& vertex : final->vertices())
        if (vertex.id.valid() && vertex.id.value() > 102 && vertex.id.value() <= 130) return false;
    return true;
}

bool test_multiple_faces_are_independent_items()
{
    phoenix::extrusion::ProfileSegment segment;
    segment.delta_y = 1;
    segment.face_label = phoenix::LabelId{70};
    segment.left_label = phoenix::LabelId{71};
    segment.bottom_label = phoenix::LabelId{72};
    segment.right_label = phoenix::LabelId{73};
    segment.top_label = phoenix::LabelId{74};
    segment.skirt_label = phoenix::LabelId{75};
    phoenix::extrusion::InstructionConfig config;
    config.profile = phoenix::extrusion::Profile::create({segment});
    config.cap_label = phoenix::LabelId{85};

    phoenix::InstructionRegistry registry;
    registry.register_handler("extrusion", phoenix::extrusion::make_instruction_handler(config));
    const auto function = extrusion_function();
    const auto source = source_two_triangles();
    phoenix::GeometryPublicationLedger ledger;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(
        source, "source", "actor:multi")}};
    request.context.function_id = function.id;
    request.context.call_path = {"multi"};
    request.publication_ledger = &ledger;

    const auto result = phoenix::FunctionExecutor{registry}.run(request);
    const auto final = ledger.assemble_actor("actor:multi");
    const auto* output = result.outputs.empty()
        ? nullptr : result.outputs.front().value.as_geometry_collection();
    phoenix::GeometryPublicationLedger threaded_ledger;
    auto threaded_request = request;
    threaded_request.publication_ledger = &threaded_ledger;
    threaded_request.options.worker_count = 4;
    const auto threaded_result = phoenix::FunctionExecutor{registry}.run(threaded_request);
    const auto threaded_final = threaded_ledger.assemble_actor("actor:multi");
    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.failures.empty() && output && output->contributions.size() == 2
        && final && final->faces().size() == 8
        && ledger.is_consumed({"actor:multi", phoenix::FaceId{130}})
        && ledger.is_consumed({"actor:multi", phoenix::FaceId{230}})
        && threaded_result.status == phoenix::FunctionExecutionStatus::completed
        && threaded_result.failures.empty() && threaded_final
        && final->fingerprint() == threaded_final->fingerprint();
}

bool test_failed_face_routes_alone_and_is_not_consumed()
{
    phoenix::extrusion::ProfileSegment segment;
    segment.delta_y = 1;
    segment.face_label = phoenix::LabelId{70};
    phoenix::extrusion::InstructionConfig config;
    config.profile = phoenix::extrusion::Profile::create({segment});
    config.cap_label = phoenix::LabelId{85};

    bool fallback_saw_only_failed_face = false;
    phoenix::InstructionRegistry registry;
    registry.register_handler("extrusion", phoenix::extrusion::make_instruction_handler(config));
    registry.register_handler("fallback",
        [&fallback_saw_only_failed_face](const phoenix::InstructionExecutionFrame& frame) {
            const phoenix::GeometryValue* geometry = nullptr;
            for (const auto& input : frame.inputs.promised_inputs)
                if (input.port == "geometry") geometry = input.value.as_geometry();
            fallback_saw_only_failed_face = geometry && geometry->geometry
                && geometry->geometry->faces().size() == 1
                && geometry->geometry->faces()[0].id == phoenix::FaceId{230};
            return phoenix::InstructionResult{
                frame.inputs.node_id,
                {{"result", phoenix::RuntimeValue::geometry("fallback")}},
                std::nullopt};
        });
    const auto function = extrusion_failure_function();
    const auto source = source_two_triangles(true);
    phoenix::GeometryPublicationLedger ledger;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(
        source, "source", "actor:failure")}};
    request.context.function_id = function.id;
    request.context.call_path = {"failure"};
    request.publication_ledger = &ledger;

    const auto result = phoenix::FunctionExecutor{registry}.run(request);
    const auto final = ledger.assemble_actor("actor:failure");
    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.failures.size() == 1 && result.failures[0].item_key == 1
        && fallback_saw_only_failed_face
        && ledger.is_consumed({"actor:failure", phoenix::FaceId{130}})
        && !ledger.is_consumed({"actor:failure", phoenix::FaceId{230}})
        && final && final->faces().size() == 5;
}

bool test_two_consuming_branches_keep_both_replacements()
{
    phoenix::extrusion::ProfileSegment segment;
    segment.delta_y = 1;
    segment.face_label = phoenix::LabelId{70};
    phoenix::extrusion::InstructionConfig config;
    config.profile = phoenix::extrusion::Profile::create({segment});
    config.cap_label = phoenix::LabelId{85};
    phoenix::InstructionRegistry registry;
    registry.register_handler("extrusion", phoenix::extrusion::make_instruction_handler(config));
    const auto function = extrusion_branches_function();
    phoenix::GeometryPublicationLedger ledger;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(
        source_triangle(), "source", "actor:branches")}};
    request.context.function_id = function.id;
    request.context.call_path = {"branches"};
    request.options.worker_count = 4;
    request.publication_ledger = &ledger;

    const auto result = phoenix::FunctionExecutor{registry}.run(request);
    const auto final = ledger.assemble_actor("actor:branches");
    return result.status == phoenix::FunctionExecutionStatus::completed
        && result.failures.empty() && final && final->faces().size() == 8
        && ledger.is_consumed({"actor:branches", phoenix::FaceId{130}});
}

bool test_cached_effect_replay_and_scope_replacement()
{
    phoenix::extrusion::ProfileSegment segment;
    segment.delta_y = 1;
    segment.face_label = phoenix::LabelId{70};
    phoenix::extrusion::InstructionConfig config;
    config.profile = phoenix::extrusion::Profile::create({segment});
    config.cap_label = phoenix::LabelId{85};

    std::size_t kernel_invocations = 0;
    const auto extrusion = phoenix::extrusion::make_instruction_handler(config);
    phoenix::InstructionRegistry registry;
    registry.register_handler("extrusion",
        [&kernel_invocations, extrusion](const phoenix::InstructionExecutionFrame& frame) {
            ++kernel_invocations;
            return extrusion(frame);
        });
    const auto function = extrusion_function();
    const auto source = source_triangle();
    phoenix::MemoryCacheStore cache;
    phoenix::GeometryPublicationLedger ledger;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(
        source, "source", "actor:cached")}};
    request.context.function_id = function.id;
    request.context.call_path = {"cached"};
    request.publication_ledger = &ledger;
    request.cache_store = &cache;
    request.cache_writer = &cache;
    request.label_registry_fingerprint = 1234;
    request.kernel_version = "extrusion-production-v1";
    request.adapter_version = config.repair_policy.version.adapter;
    request.repair_policy_version = config.repair_policy.version.repair;

    const phoenix::FunctionExecutor executor{registry};
    const auto uncached = executor.run(request);
    const auto first = ledger.assemble_actor("actor:cached");
    const auto cached = executor.run(request);
    const auto replayed = ledger.assemble_actor("actor:cached");
    if (uncached.status != phoenix::FunctionExecutionStatus::completed
        || cached.status != phoenix::FunctionExecutionStatus::completed
        || kernel_invocations != 1 || !first || !replayed
        || first->fingerprint() != replayed->fingerprint()
        || !ledger.is_consumed({"actor:cached", phoenix::FaceId{130}})) return false;

    phoenix::InstructionRegistry failing_registry;
    failing_registry.register_handler("extrusion",
        [](const phoenix::InstructionExecutionFrame& frame) {
            phoenix::InstructionResult result;
            result.node_id = frame.inputs.node_id;
            phoenix::GeometryItemEffect effect;
            effect.item_key = 0;
            effect.succeeded = false;
            effect.failure_message = "replacement no longer succeeds";
            result.geometry_effects.push_back(std::move(effect));
            return result;
        });
    request.kernel_version = "extrusion-production-v2";
    const auto changed = phoenix::FunctionExecutor{failing_registry}.run(request);
    const auto restored = ledger.assemble_actor("actor:cached");
    return changed.status == phoenix::FunctionExecutionStatus::completed
        && restored && restored->faces().size() == 1
        && restored->faces()[0].id == phoenix::FaceId{130}
        && !ledger.is_consumed({"actor:cached", phoenix::FaceId{130}});
}

bool test_end_to_end_partial_rerun_apply()
{
    auto fail = [](const char* stage) {
        std::cerr << "partial rerun failure stage: " << stage << '\n';
        return false;
    };
    phoenix::extrusion::ProfileSegment segment;
    segment.delta_y = 1;
    segment.face_label = phoenix::LabelId{70};
    phoenix::extrusion::InstructionConfig config;
    config.profile = phoenix::extrusion::Profile::create({segment});
    config.cap_label = phoenix::LabelId{85};
    std::size_t runs = 0;
    const auto extrusion = phoenix::extrusion::make_instruction_handler(config);
    phoenix::InstructionRegistry registry;
    registry.register_handler("extrusion", [&runs, extrusion](const auto& frame) {
        ++runs;
        return extrusion(frame);
    });
    auto function = extrusion_function();
    function.id = "extrusion-partial-e2e";
    function.generates_actor = true;
    const auto source = source_triangle();
    phoenix::MemoryCacheStore cache;
    phoenix::GeometryPublicationLedger ledger;
    phoenix::RunElementIdAllocator ids;
    phoenix::FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"geometry", phoenix::RuntimeValue::geometry(
        source, "source", "actor:partial-e2e")}};
    request.context.function_id = function.id;
    request.context.call_path = {"partial-e2e"};
    request.publication_ledger = &ledger;
    request.element_ids = &ids;
    request.cache_store = &cache;
    request.cache_writer = &cache;
    request.label_registry_fingerprint = 1234;
    request.kernel_version = "extrusion-production-v1";
    request.adapter_version = config.repair_policy.version.adapter;
    request.repair_policy_version = config.repair_policy.version.repair;
    const phoenix::FunctionExecutor executor{registry};
    const auto full = executor.run(request);
    if (!full.actor || !full.actor->geometry || !full.actor->geometry->geometry) return fail("full");
    const auto full_fingerprint = full.actor->geometry->geometry->fingerprint();
    phoenix::SceneRoot scene;
    scene.root.id = "scene-root";
    scene.root.children.push_back(*full.actor);
    const auto identity = phoenix::CacheIdentityBuilder{}.identity({
        &function, request.context.call_path, request.inputs, request.context.global_seed,
        request.label_registry_fingerprint, request.kernel_version,
        request.adapter_version, request.repair_policy_version});
    phoenix::PartialRerunRequest plan_request;
    plan_request.function = &function;
    plan_request.changed_instructions = {1};
    plan_request.cache_identity = identity;
    plan_request.actor_id = "actor:partial-e2e";
    plan_request.cache_store = &cache;
    const auto plan = phoenix::PartialRerunPlanner{}.plan(plan_request);
    if (plan.actor_subtree_cache_hit || plan.instructions.empty()) return fail("plan");
    phoenix::PartialRerunScopeRequest scope_request;
    scope_request.plan = &plan;
    scope_request.function = &function;
    scope_request.inputs = request.inputs;
    scope_request.context = request.context;
    scope_request.actor_id = "actor:partial-e2e";
    scope_request.cache_store = &cache;
    scope_request.cache_writer = &cache;
    scope_request.publication_ledger = &ledger;
    scope_request.element_ids = &ids;
    scope_request.label_registry_fingerprint = request.label_registry_fingerprint;
    scope_request.kernel_version = request.kernel_version;
    scope_request.adapter_version = request.adapter_version;
    scope_request.repair_policy_version = request.repair_policy_version;
    const auto scope = phoenix::PartialRerunScopeResolver{}.resolve(scope_request);
    if (!scope.execution_request) return fail("scope");
    const auto applied = phoenix::PartialRerunApplier{}.apply(
        {&scene, &plan, &cache, &executor, &*scope.execution_request});
    const auto cached_geometry = scene.root.children[0].geometry
        ? scene.root.children[0].geometry->geometry : nullptr;
    if (applied.status != phoenix::PartialRerunApplyStatus::applied_rerun_actor_subtree
        || runs != 1 || !cached_geometry
        || cached_geometry->fingerprint() != full_fingerprint) return fail("cached apply");

    phoenix::InstructionRegistry changed_registry;
    changed_registry.register_handler("extrusion", [](const auto& frame) {
        phoenix::InstructionResult result;
        result.node_id = frame.inputs.node_id;
        result.geometry_effects.push_back({0, false, nullptr, {}, "changed failure"});
        return result;
    });
    scope_request.kernel_version = "extrusion-production-v2";
    plan_request.cache_identity = phoenix::CacheIdentityBuilder{}.identity({
        &function, request.context.call_path, request.inputs, request.context.global_seed,
        request.label_registry_fingerprint, scope_request.kernel_version,
        request.adapter_version, request.repair_policy_version});
    const auto changed_plan = phoenix::PartialRerunPlanner{}.plan(plan_request);
    scope_request.plan = &changed_plan;
    const auto changed_scope = phoenix::PartialRerunScopeResolver{}.resolve(scope_request);
    if (!changed_scope.execution_request) return fail("changed scope");
    const phoenix::FunctionExecutor changed_executor{changed_registry};
    const auto changed = phoenix::PartialRerunApplier{}.apply(
        {&scene, &changed_plan, &cache, &changed_executor, &*changed_scope.execution_request});
    const auto restored = scene.root.children[0].geometry
        ? scene.root.children[0].geometry->geometry : nullptr;
    const bool restored_ok = changed.status == phoenix::PartialRerunApplyStatus::applied_rerun_actor_subtree
        && restored && restored->faces().size() == 1
        && restored->faces()[0].id == phoenix::FaceId{130}
        && !ledger.is_consumed({"actor:partial-e2e", phoenix::FaceId{130}});
    return restored_ok || fail("changed apply");
}

} // namespace

int main()
{
    const bool publication = test_executor_consumes_and_publishes_replacement();
    const bool multiple = test_multiple_faces_are_independent_items();
    const bool failure = test_failed_face_routes_alone_and_is_not_consumed();
    const bool branches = test_two_consuming_branches_keep_both_replacements();
    const bool cache = test_cached_effect_replay_and_scope_replacement();
    const bool partial = test_end_to_end_partial_rerun_apply();
    const bool ok = publication && multiple && failure && branches && cache && partial;
    std::cout << "extrusion execution/publication: " << publication << '\n'
              << "multi-face items: " << multiple << '\n'
              << "per-face failure/else: " << failure << '\n'
              << "two consuming branches: " << branches << '\n'
              << "cache replay/scope replacement: " << cache << '\n'
              << "end-to-end partial rerun apply: " << partial << '\n';
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
