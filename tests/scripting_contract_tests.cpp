#include "phoenix/scripting/contract.hpp"

#include <iostream>

namespace {

bool local_bindings_shadow_globals()
{
    phoenix::scripting::EvaluationRequest request;
    request.program.source = "height + width";
    request.global_bindings = {{"height", std::int64_t{2}}, {"width", 3.0}};
    request.local_bindings = {{"height", std::int64_t{7}}};
    const auto result = phoenix::scripting::validate_request(request);
    return result.success() && std::get<std::int64_t>(result.effective_bindings.at("height")) == 7;
}

bool rejects_invalid_contract_inputs()
{
    phoenix::scripting::EvaluationRequest request;
    request.global_bindings = {{"bad-name", true}};
    const auto result = phoenix::scripting::validate_request(request);
    return !result.success() && result.diagnostics.size() == 2;
}

bool cache_identity_is_complete_and_stable()
{
    phoenix::scripting::EvaluationRequest a;
    a.program.source = "[height] * 2";
    a.local_bindings = {{"height", 4.0}};
    a.deterministic_seed = 91;
    auto b = a;
    const auto first = phoenix::scripting::cache_fingerprint(a, "candidate", "1");
    const auto same = phoenix::scripting::cache_fingerprint(b, "candidate", "1");
    b.local_bindings["height"] = 5.0;
    const auto changed = phoenix::scripting::cache_fingerprint(b, "candidate", "1");
    const auto engine_changed = phoenix::scripting::cache_fingerprint(a, "candidate", "2");
    return first == same && first != changed && first != engine_changed;
}

} // namespace

int main()
{
    const bool scope = local_bindings_shadow_globals();
    const bool validation = rejects_invalid_contract_inputs();
    const bool cache = cache_identity_is_complete_and_stable();
    std::cout << "script local binding precedence: " << scope << '\n'
              << "script contract validation: " << validation << '\n'
              << "script cache identity: " << cache << '\n';
    return scope && validation && cache ? 0 : 1;
}
