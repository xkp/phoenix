#include "phoenix/loop/geometry_transaction.hpp"

namespace phoenix::loop {
namespace {

void append_geometry(std::vector<CanonicalGeometryRef>& geometries, const RuntimeValue& value)
{
    if (const auto* contribution = value.as_geometry()) {
        if (contribution->geometry) geometries.push_back(contribution->geometry);
    } else if (const auto* collection = value.as_geometry_collection()) {
        for (const auto& item : collection->contributions)
            if (item.geometry) geometries.push_back(item.geometry);
    }
}

CanonicalGeometryRef combine(const std::vector<CanonicalGeometryRef>& geometries,
    const ActorId& owner)
{
    if (geometries.empty()) return nullptr;
    GeometryPublicationLedger ledger;
    std::vector<GeometryItemEffect> effects;
    effects.reserve(geometries.size());
    for (std::size_t i = 0; i < geometries.size(); ++i) {
        GeometryItemEffect effect;
        effect.item_key = i;
        effect.generated_geometry = geometries[i];
        effects.push_back(std::move(effect));
    }
    const auto commit = ledger.replace_scope(
        {"loop-collapse", {"loop-collapse"}, 0}, owner, false, std::move(effects));
    (void)commit;
    return ledger.assemble_actor(owner);
}

} // namespace

GeometryTransactionResult run_geometry_transaction(const GeometryTransactionRequest& request)
{
    GeometryTransactionResult result;
    result.publication.item_key = request.item_key;
    result.publication.succeeded = false;
    if (!request.source.geometry) {
        result.loop.error = "Loop geometry transaction requires canonical source geometry.";
        result.publication.failure_message = result.loop.error;
        return result;
    }

    GeometryPublicationLedger staging;
    staging.set_actor_source(request.owner_actor_id, request.source.geometry);
    auto body_request = request.body;
    body_request.staging_publication_ledger = &staging;
    RunRequest run_request;
    run_request.options = request.options;
    run_request.input = RuntimeValue::geometry(request.source.geometry,
        request.source.debug_label, request.owner_actor_id);
    run_request.seed = request.seed;
    run_request.body = make_function_body(std::move(body_request));
    run_request.trace_sink = request.trace_sink;
    if (request.variables) {
        const auto initialized = scripting::initialize_variables(*request.variables);
        if (!initialized.success()) {
            result.loop.error = initialized.error;
            result.publication.failure_message = result.loop.error;
            return result;
        }
        run_request.initial_variables = *initialized.values;
        const auto plan = *request.variables;
        run_request.update_variables = [plan](const scripting::Bindings& previous,
            std::size_t index, SeedValue seed) {
            const auto evaluated = scripting::update_variables(plan, previous, index, seed);
            return VariableUpdateResult{evaluated.success(),
                evaluated.values.value_or(scripting::Bindings{}), evaluated.error};
        };
    }
    result.loop = run(run_request);
    if (!result.loop.success) {
        result.publication.failure_message = result.loop.error;
        return result;
    }

    std::vector<CanonicalGeometryRef> accumulated;
    for (const auto& value : result.loop.accumulated) append_geometry(accumulated, value);
    auto generated = combine(accumulated, request.owner_actor_id);
    if (!generated) {
        // Production emits no loop output in this case. With no replacement,
        // the outer instruction must also consume nothing.
        result.publication.succeeded = true;
        return result;
    }
    result.publication.succeeded = true;
    result.publication.generated_geometry = std::move(generated);
    for (const auto& face : request.source.geometry->faces())
        result.publication.consumed_faces.push_back({request.owner_actor_id, face.id});
    return result;
}

} // namespace phoenix::loop
