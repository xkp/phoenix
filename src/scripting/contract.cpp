#include "phoenix/scripting/contract.hpp"

#include <cctype>
#include <cstring>
#include <type_traits>

namespace phoenix::scripting {
namespace {

constexpr std::uint64_t offset = 1469598103934665603ULL;
constexpr std::uint64_t prime = 1099511628211ULL;

void hash_bytes(std::uint64_t& hash, const void* bytes, std::size_t size) noexcept
{
    const auto* data = static_cast<const unsigned char*>(bytes);
    for (std::size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * prime;
}

void hash_string(std::uint64_t& hash, const std::string& value) noexcept
{
    hash_bytes(hash, value.data(), value.size());
    const unsigned char terminator = 0xff;
    hash_bytes(hash, &terminator, 1);
}

bool valid_name(const std::string& name) noexcept
{
    if (name.empty()) return false;
    const auto first = static_cast<unsigned char>(name.front());
    if (!(std::isalpha(first) || first == '_')) return false;
    for (const auto character : name) {
        const auto value = static_cast<unsigned char>(character);
        if (!(std::isalnum(value) || value == '_')) return false;
    }
    return true;
}

void hash_value(std::uint64_t& hash, const Value& value) noexcept
{
    const auto index = static_cast<std::uint64_t>(value.index());
    hash_bytes(hash, &index, sizeof(index));
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::string>) hash_string(hash, item);
        else hash_bytes(hash, &item, sizeof(item));
    }, value);
}

} // namespace

bool EvaluationResult::success() const noexcept
{
    return status == EvaluationStatus::completed && value.has_value();
}

bool ScriptResult::success() const noexcept
{
    return status == EvaluationStatus::completed;
}

bool ValidationResult::success() const noexcept { return diagnostics.empty(); }

ValidationResult validate_request(const EvaluationRequest& request)
{
    ValidationResult result;
    if (request.program.source.empty())
        result.diagnostics.push_back({DiagnosticCode::empty_source,
            "Expression source is empty.", {}, {}});
    auto add = [&](const Bindings& bindings, bool local) {
        for (const auto& binding : bindings) {
            if (!valid_name(binding.first)) {
                result.diagnostics.push_back({DiagnosticCode::invalid_binding_name,
                    "Invalid binding name: " + binding.first, {}, {}});
                continue;
            }
            const auto found = result.effective_bindings.find(binding.first);
            if (found != result.effective_bindings.end() && !local) {
                result.diagnostics.push_back({DiagnosticCode::duplicate_binding,
                    "Duplicate global binding: " + binding.first, {}, {}});
                continue;
            }
            // Instruction-local values intentionally shadow function/global values.
            result.effective_bindings[binding.first] = binding.second;
        }
    };
    add(request.global_bindings, false);
    add(request.local_bindings, true);
    return result;
}

std::uint64_t cache_fingerprint(const EvaluationRequest& request,
    const std::string& engine_id, const std::string& engine_version) noexcept
{
    std::uint64_t hash = offset;
    hash_string(hash, request.program.language);
    hash_bytes(hash, &request.program.language_version,
        sizeof(request.program.language_version));
    hash_string(hash, request.program.source);
    hash_string(hash, engine_id);
    hash_string(hash, engine_version);
    const auto validated = validate_request(request);
    for (const auto& binding : validated.effective_bindings) {
        hash_string(hash, binding.first);
        hash_value(hash, binding.second);
    }
    hash_bytes(hash, &request.deterministic_seed, sizeof(request.deterministic_seed));
    hash_bytes(hash, &request.limits.instruction_budget,
        sizeof(request.limits.instruction_budget));
    hash_bytes(hash, &request.limits.memory_bytes, sizeof(request.limits.memory_bytes));
    hash_bytes(hash, &request.limits.recursion_depth,
        sizeof(request.limits.recursion_depth));
    return hash;
}

std::string to_string(DiagnosticCode code)
{
    switch (code) {
    case DiagnosticCode::empty_source: return "empty_source";
    case DiagnosticCode::invalid_binding_name: return "invalid_binding_name";
    case DiagnosticCode::duplicate_binding: return "duplicate_binding";
    case DiagnosticCode::compile_error: return "compile_error";
    case DiagnosticCode::evaluation_error: return "evaluation_error";
    case DiagnosticCode::unsupported_result: return "unsupported_result";
    case DiagnosticCode::instruction_budget_exceeded: return "instruction_budget_exceeded";
    case DiagnosticCode::memory_budget_exceeded: return "memory_budget_exceeded";
    case DiagnosticCode::cancelled: return "cancelled";
    case DiagnosticCode::engine_unavailable: return "engine_unavailable";
    }
    return "unknown";
}

} // namespace phoenix::scripting
