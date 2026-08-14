#include "phoenix/loop/instruction.hpp"
#include "phoenix/partial_run_apply.hpp"
#include "phoenix/partial_run_scope.hpp"

#include <iostream>

namespace {

phoenix::CanonicalGeometryRef triangle()
{
    using namespace phoenix;
    return CanonicalGeometry::create({
        {{0, 0, 0}, VertexId{1}, 0}, {{1, 0, 0}, VertexId{2}, 1},
        {{0, 1, 0}, VertexId{3}, 2}}, {
        {0, 0, 1, 2, invalid_geometry_index, 0, HalfedgeId{10}, EdgeId{20}, {}},
        {1, 0, 2, 0, invalid_geometry_index, 1, HalfedgeId{11}, EdgeId{21}, {}},
        {2, 0, 0, 1, invalid_geometry_index, 2, HalfedgeId{12}, EdgeId{22}, {}}},
        {{0, FaceId{40}, LabelId{70}}});
}

phoenix::FunctionDescriptor loop_body()
{
    using namespace phoenix;
    FunctionDescriptor function;
    function.id = "loop-body-l3";
    function.input_ports = {{"input", "geometry", PortDirection::input},
        {"$index", "integer", PortDirection::input}};
    function.output_ports = {{"loop", "geometry", PortDirection::output},
        {"output", "geometry", PortDirection::output}};
    InstructionDescriptor body;
    body.id = 10; body.kind = "body-l3";
    body.input_ports = function.input_ports; body.output_ports = function.output_ports;
    body.consumes_geometry = true;
    InstructionDescriptor output;
    output.id = 99; output.kind = "output"; output.input_ports = function.output_ports;
    function.instructions = {body, output};
    function.edges = {{10, "loop", 99, "loop"}, {10, "output", 99, "output"}};
    function.output_node_id = 99;
    return function;
}

phoenix::FunctionDescriptor outer_function(const std::string& revision = {})
{
    using namespace phoenix;
    FunctionDescriptor function;
    function.id = "loop-outer-l3";
    function.input_ports = {{"input", "geometry", PortDirection::input}};
    function.output_ports = {{"output", "geometry", PortDirection::output}};
    InstructionDescriptor loop;
    loop.id = 1; loop.kind = "loop-l3";
    loop.input_ports = function.input_ports; loop.output_ports = function.output_ports;
    loop.consumes_geometry = true;
    loop.configuration_revision = revision;
    InstructionDescriptor output;
    output.id = 99; output.kind = "output"; output.input_ports = function.output_ports;
    function.instructions = {loop, output};
    function.edges = {{1, "output", 99, "output"}};
    function.output_node_id = 99;
    return function;
}

const phoenix::RuntimeValue* input(const phoenix::InstructionExecutionFrame& frame,
    const phoenix::PortId& port)
{
    for (const auto& value : frame.inputs.promised_inputs)
        if (value.port == port) return &value.value;
    return nullptr;
}

struct Trace final : phoenix::loop::TraceSink {
    std::vector<phoenix::loop::TraceEvent> events;
    void record(phoenix::loop::TraceEvent event) override { events.push_back(std::move(event)); }
};

bool publication_cache_and_trace()
{
    using namespace phoenix;
    std::size_t body_runs = 0;
    InstructionRegistry registry;
    registry.register_handler("body-l3", [&body_runs](const auto& frame) {
        ++body_runs;
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        const auto* source = input(frame, "input")->as_geometry();
        GeometryItemEffect effect;
        effect.generated_geometry = source->geometry;
        effect.consumed_faces.push_back({"actor:loop-l3", source->geometry->faces()[0].id});
        result.geometry_effects.push_back(std::move(effect));
        result.produced_outputs.push_back({"loop", *input(frame, "input")});
        result.produced_outputs.push_back({"output", *input(frame, "input")});
        return result;
    });
    const auto body = loop_body();
    const FunctionExecutor executor{registry};
    Trace trace;
    loop::InstructionConfig config;
    config.options.count = 2;
    config.body.executor = &executor;
    config.body.function = &body;
    config.trace_sink = &trace;
    const auto loop_handler = loop::make_instruction_handler(config);
    std::size_t generated_faces = 0;
    registry.register_handler("loop-l3", [&generated_faces, loop_handler](const auto& frame) {
        auto result = loop_handler(frame);
        if (!result.geometry_effects.empty() && result.geometry_effects[0].generated_geometry)
            generated_faces = result.geometry_effects[0].generated_geometry->faces().size();
        return result;
    });
    const auto outer = outer_function(loop::configuration_revision(config.options, body.id));
    GeometryPublicationLedger ledger;
    MemoryCacheStore cache;
    RunElementIdAllocator ids(1000);
    FunctionExecutionRequest request;
    request.function = &outer;
    request.inputs = {{"input", RuntimeValue::geometry(
        triangle(), "source", ActorId{"actor:loop-l3"})}};
    request.context = {outer.id, {"loop-l3"}, 33};
    request.publication_ledger = &ledger;
    request.element_ids = &ids;
    request.cache_store = &cache;
    request.cache_writer = &cache;
    request.kernel_version = "loop-runtime-v1";
    request.adapter_version = "loop-function-body-v1";
    const auto first = executor.run(request);
    const auto assembled = ledger.assemble_actor("actor:loop-l3");
    const auto trace_count = trace.events.size();
    const auto second = executor.run(request);
    return first.status == FunctionExecutionStatus::completed && first.failures.empty()
        && assembled && assembled->faces().size() == 2
        && ledger.is_consumed({"actor:loop-l3", FaceId{40}})
        && second.status == FunctionExecutionStatus::completed && generated_faces == 2
        && body_runs == 2 && trace_count > 0 && trace.events.size() == trace_count;
}

bool options_change_graph_and_cache_identity()
{
    phoenix::loop::Options first;
    first.count = 2;
    auto second = first;
    second.count = 3;
    const auto first_function = outer_function(
        phoenix::loop::configuration_revision(first, "body"));
    const auto second_function = outer_function(
        phoenix::loop::configuration_revision(second, "body"));
    return phoenix::CacheIdentityBuilder{}.graph_revision(first_function)
        != phoenix::CacheIdentityBuilder{}.graph_revision(second_function);
}

bool variable_expressions_update_atomically_and_change_identity()
{
    using namespace phoenix;
    loop::InstructionConfig config;
    config.options.count=3;
    scripting::VariablePlan variables;
    scripting::VariableSpec value;value.name="value";value.initial_value=std::int64_t{2};
    scripting::ExpressionSpec update;update.program.source="value + _index";value.update=update;
    variables.variables.push_back(value);config.variables=variables;
    const auto initialized=scripting::initialize_variables(variables);
    const auto first=scripting::update_variables(variables,*initialized.values,1,7);
    const auto second=scripting::update_variables(variables,*first.values,2,8);
    auto changed=config;changed.variables->variables[0].update->program.source="value + 1";
    auto changed_initial=config;changed_initial.variables->variables[0].initial_value=std::int64_t{3};
    return initialized.success()&&first.success()&&second.success()
        &&std::get<std::int64_t>(second.values->at("value"))==5
        &&loop::configuration_revision(config)!=loop::configuration_revision(changed)
        &&loop::configuration_revision(config)!=loop::configuration_revision(changed_initial);
}

bool changed_loop_scope_failure_restores_source()
{
    using namespace phoenix;
    InstructionRegistry registry;
    registry.register_handler("loop-l3", [](const auto& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        GeometryItemEffect effect;
        effect.generated_geometry = triangle();
        effect.consumed_faces.push_back({"actor:loop-restore", FaceId{40}});
        result.geometry_effects.push_back(std::move(effect));
        result.produced_outputs.push_back({"output", RuntimeValue::geometry(
            triangle(), "loop", ActorId{"actor:loop-restore"})});
        return result;
    });
    auto function = outer_function("loop-config-v1");
    GeometryPublicationLedger ledger;
    MemoryCacheStore cache;
    FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"input", RuntimeValue::geometry(
        triangle(), "source", ActorId{"actor:loop-restore"})}};
    request.context = {function.id, {"loop-restore"}, 7};
    request.publication_ledger = &ledger;
    request.cache_store = &cache;
    request.cache_writer = &cache;
    const FunctionExecutor executor{registry};
    const auto first = executor.run(request);
    if (first.status != FunctionExecutionStatus::completed
        || !ledger.is_consumed({"actor:loop-restore", FaceId{40}})) return false;

    registry.register_handler("loop-l3", [](const auto& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        result.geometry_effects.push_back({0, false, nullptr, {}, "changed loop failure"});
        return result;
    });
    function.instructions[0].configuration_revision = "loop-config-v2";
    const auto changed = executor.run(request);
    const auto restored = ledger.assemble_actor("actor:loop-restore");
    return changed.status == FunctionExecutionStatus::completed
        && restored && restored->faces().size() == 1
        && restored->faces()[0].id == FaceId{40}
        && !ledger.is_consumed({"actor:loop-restore", FaceId{40}});
}

bool partial_rerun_replays_and_restores()
{
    using namespace phoenix;
    std::size_t body_runs = 0;
    InstructionRegistry registry;
    registry.register_handler("body-l3", [&body_runs](const auto& frame) {
        ++body_runs;
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        const auto* source = input(frame, "input")->as_geometry();
        GeometryItemEffect effect;
        effect.generated_geometry = source->geometry;
        effect.consumed_faces.push_back({"actor:loop-partial", source->geometry->faces()[0].id});
        result.geometry_effects.push_back(std::move(effect));
        result.produced_outputs.push_back({"loop", *input(frame, "input")});
        result.produced_outputs.push_back({"output", *input(frame, "input")});
        return result;
    });
    const auto body = loop_body();
    const FunctionExecutor executor{registry};
    loop::InstructionConfig config;
    config.options.count = 2;
    config.body.executor = &executor;
    config.body.function = &body;
    registry.register_handler("loop-l3", loop::make_instruction_handler(config));
    auto function = outer_function(loop::configuration_revision(config.options, body.id));
    function.id = "loop-partial";
    function.generates_actor = true;
    MemoryCacheStore cache;
    GeometryPublicationLedger ledger;
    RunElementIdAllocator ids(3000);
    FunctionExecutionRequest request;
    request.function = &function;
    request.inputs = {{"input", RuntimeValue::geometry(
        triangle(), "source", ActorId{"actor:loop-partial"})}};
    request.context = {function.id, {"loop-partial"}, 17};
    request.publication_ledger = &ledger;
    request.element_ids = &ids;
    request.cache_store = &cache;
    request.cache_writer = &cache;
    request.label_registry_fingerprint = 1234;
    request.kernel_version = "loop-runtime-v1";
    request.adapter_version = "loop-function-body-v1";
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
    plan_request.actor_id = "actor:loop-partial";
    plan_request.cache_store = &cache;
    const auto plan = PartialRerunPlanner{}.plan(plan_request);
    PartialRerunScopeRequest scope_request;
    scope_request.plan = &plan;
    scope_request.function = &function;
    scope_request.inputs = request.inputs;
    scope_request.context = request.context;
    scope_request.actor_id = "actor:loop-partial";
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
        || body_runs != 2 || !replayed || replayed->fingerprint() != fingerprint)
        return false;

    InstructionRegistry failing_registry;
    failing_registry.register_handler("loop-l3", [](const auto& frame) {
        InstructionResult result;
        result.node_id = frame.inputs.node_id;
        result.geometry_effects.push_back({0, false, nullptr, {}, "changed loop failure"});
        return result;
    });
    scope_request.kernel_version = "loop-runtime-v2";
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
        && !ledger.is_consumed({"actor:loop-partial", FaceId{40}});
}

} // namespace

int main()
{
    const bool result = publication_cache_and_trace();
    const bool identity = options_change_graph_and_cache_identity();
    const bool restoration = changed_loop_scope_failure_restores_source();
    const bool partial = partial_rerun_replays_and_restores();
    const bool variables = variable_expressions_update_atomically_and_change_identity();
    std::cout << "loop instruction publication, cache, trace: " << result << '\n'
              << "loop option cache identity: " << identity << '\n'
              << "loop changed-scope restoration: " << restoration << '\n';
    std::cout << "loop partial-rerun replay and restoration: " << partial << '\n';
    std::cout << "loop expression variables and identity: " << variables << '\n';
    return result && identity && restoration && partial && variables ? 0 : 1;
}
