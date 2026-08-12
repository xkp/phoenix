#include "phoenix/labels.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <sstream>

namespace phoenix {
namespace {

void set_diagnostic(
    LabelDiagnostic* diagnostic,
    LabelDiagnosticCode code,
    const LabelUid& uid,
    const LabelOrigin& origin,
    const std::string& detail)
{
    if (diagnostic == nullptr) return;
    diagnostic->code = code;
    diagnostic->uid = uid;
    diagnostic->function_id = origin.function_id;
    diagnostic->message = detail;
}

void hash_byte(std::uint64_t& hash, unsigned char value) noexcept
{
    hash ^= value;
    hash *= 1099511628211ULL;
}

void hash_string(std::uint64_t& hash, const std::string& value) noexcept
{
    for (const auto ch : value) hash_byte(hash, static_cast<unsigned char>(ch));
    hash_byte(hash, 0xffU);
}

} // namespace

bool LabelRegistry::add(
    const LabelUid& uid,
    const LabelDefinition& definition,
    LabelOrigin origin,
    LabelDiagnostic* diagnostic)
{
    if (frozen_) {
        set_diagnostic(diagnostic, LabelDiagnosticCode::registry_frozen, uid, origin,
            "Cannot add label '" + uid + "' after the registry is frozen.");
        return false;
    }
    if (uid.empty()) {
        set_diagnostic(diagnostic, LabelDiagnosticCode::empty_uid, uid, origin,
            "Label UID cannot be empty.");
        return false;
    }

    const auto found = pending_.find(uid);
    if (found != pending_.end()) {
        if (!(found->second.definition == definition)) {
            set_diagnostic(diagnostic, LabelDiagnosticCode::conflicting_definition, uid, origin,
                "Label UID '" + uid + "' has conflicting immutable definitions.");
            return false;
        }
        found->second.origins.push_back(std::move(origin));
        return true;
    }

    pending_.emplace(uid, PendingLabel{definition, {std::move(origin)}});
    return true;
}

void LabelRegistry::freeze()
{
    if (frozen_) return;

    std::vector<LabelDefinition> unique;
    unique.reserve(pending_.size());
    for (const auto& entry : pending_) unique.push_back(entry.second.definition);
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());

    if (unique.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::length_error("Too many registered labels.");
    }
    definitions_ = unique;

    for (const auto& entry : pending_) {
        const auto definition_it = std::lower_bound(unique.begin(), unique.end(), entry.second.definition);
        by_uid_.emplace(entry.first, LabelId{static_cast<std::int32_t>(definition_it - unique.begin())});
    }
    frozen_ = true;
}

std::optional<LabelId> LabelRegistry::find_uid(const LabelUid& uid) const noexcept
{
    const auto found = by_uid_.find(uid);
    return found == by_uid_.end() ? std::nullopt : std::optional<LabelId>{found->second};
}

const LabelDefinition* LabelRegistry::find_definition(LabelId id) const noexcept
{
    if (!id.is_registered() || static_cast<std::size_t>(id.value()) >= definitions_.size()) return nullptr;
    return &definitions_[static_cast<std::size_t>(id.value())];
}

const std::vector<LabelOrigin>* LabelRegistry::origins(const LabelUid& uid) const noexcept
{
    const auto found = pending_.find(uid);
    return found == pending_.end() ? nullptr : &found->second.origins;
}

std::uint64_t LabelRegistry::semantic_fingerprint() const noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& entry : by_uid_) {
        hash_string(hash, entry.first);
        const auto* definition = find_definition(entry.second);
        if (definition == nullptr) continue;
        hash_string(hash, definition->name);
        hash_string(hash, definition->color);
        hash_string(hash, definition->material);
        hash_byte(hash, definition->hidden ? 1U : 0U);
    }
    return hash;
}

std::optional<LabelId> FunctionLabelTable::resolve(const LabelUid& uid) const noexcept
{
    const auto found = by_uid_.find(uid);
    return found == by_uid_.end() ? std::nullopt : std::optional<LabelId>{found->second};
}

bool FunctionLabelTable::visible(const LabelUid& uid) const noexcept
{
    return by_uid_.find(uid) != by_uid_.end();
}

LinkedLabels LabelLinker::link(
    const FunctionDescriptor& root,
    const LabelFunctionLibrary& functions,
    const FunctionLabelDeclarations& declarations) const
{
    LinkedLabels result;
    std::map<FunctionId, const FunctionDescriptor*> reachable;
    std::vector<const FunctionDescriptor*> pending{&root};

    while (!pending.empty()) {
        const auto* function = pending.back();
        pending.pop_back();
        if (!reachable.emplace(function->id, function).second) continue;
        for (const auto& instruction : function->instructions) {
            if (!instruction.called_function_id) continue;
            const auto child = functions.find(*instruction.called_function_id);
            if (child != functions.end()) pending.push_back(&child->second);
        }
    }

    for (const auto& function_entry : reachable) {
        const auto declaration_it = declarations.find(function_entry.first);
        if (declaration_it == declarations.end()) continue;
        for (const auto& declaration : declaration_it->second) {
            LabelDiagnostic diagnostic;
            if (!result.registry.add(
                    declaration.uid,
                    declaration.definition,
                    LabelOrigin{function_entry.first, declaration.source},
                    &diagnostic)) {
                result.diagnostics.push_back(std::move(diagnostic));
            }
        }
    }

    if (!result.diagnostics.empty()) return result;
    result.registry.freeze();

    for (const auto& function_entry : reachable) {
        FunctionLabelTable table;
        const auto declaration_it = declarations.find(function_entry.first);
        if (declaration_it != declarations.end()) {
            for (const auto& declaration : declaration_it->second) {
                const auto id = result.registry.find_uid(declaration.uid);
                if (id) table.by_uid_.emplace(declaration.uid, *id);
            }
        }
        result.function_tables.emplace(function_entry.first, std::move(table));
    }

    for (const auto& function_entry : reachable) {
        const auto& table = result.function_tables.at(function_entry.first);
        for (const auto& instruction : function_entry.second->instructions) {
            for (const auto& uid : instruction.referenced_label_uids) {
                if (table.visible(uid)) continue;
                result.diagnostics.push_back(LabelDiagnostic{
                    LabelDiagnosticCode::unresolved_reference,
                    "Instruction " + std::to_string(instruction.id) + " references label UID '" + uid
                        + "' outside function '" + function_entry.first + "'.",
                    function_entry.first,
                    uid});
            }
        }
    }
    return result;
}

std::string to_string(LabelDiagnosticCode code)
{
    switch (code) {
    case LabelDiagnosticCode::empty_uid: return "empty_uid";
    case LabelDiagnosticCode::conflicting_definition: return "conflicting_definition";
    case LabelDiagnosticCode::unresolved_reference: return "unresolved_reference";
    case LabelDiagnosticCode::registry_frozen: return "registry_frozen";
    }
    return "unknown";
}

} // namespace phoenix
