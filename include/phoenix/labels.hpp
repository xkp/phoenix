#pragma once

#include "phoenix/common.hpp"
#include "phoenix/graph.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace phoenix {

class LabelId {
public:
    constexpr LabelId() noexcept = default;
    explicit constexpr LabelId(std::int32_t value) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::int32_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_registered() const noexcept { return value_ >= 0; }

    friend constexpr bool operator==(LabelId left, LabelId right) noexcept
    {
        return left.value_ == right.value_;
    }

    friend constexpr bool operator!=(LabelId left, LabelId right) noexcept
    {
        return !(left == right);
    }

    friend constexpr bool operator<(LabelId left, LabelId right) noexcept
    {
        return left.value_ < right.value_;
    }

private:
    std::int32_t value_ = -1;
};

inline constexpr LabelId unassigned_label_id{-1};
inline constexpr LabelId unbounded_label_id{-1000};
inline constexpr LabelId layout_label_id{-1001};

struct LabelDefinition {
    std::string name;
    std::string color;
    std::string material;
    bool hidden = false;
    std::string style;

    friend bool operator==(const LabelDefinition& left, const LabelDefinition& right)
    {
        return left.name == right.name
            && left.color == right.color
            && left.material == right.material
            && left.hidden == right.hidden
            && left.style == right.style;
    }

    friend bool operator<(const LabelDefinition& left, const LabelDefinition& right)
    {
        if (left.name != right.name) return left.name < right.name;
        if (left.color != right.color) return left.color < right.color;
        if (left.material != right.material) return left.material < right.material;
        if (left.hidden != right.hidden) return left.hidden < right.hidden;
        return left.style < right.style;
    }
};

struct LabelOrigin {
    FunctionId function_id;
    std::string source;
};

struct LabelDeclaration {
    LabelUid uid;
    LabelDefinition definition;
    std::string source;
};

enum class LabelDiagnosticCode {
    empty_uid,
    conflicting_definition,
    unresolved_reference,
    registry_frozen,
};

struct LabelDiagnostic {
    LabelDiagnosticCode code;
    std::string message;
    std::optional<FunctionId> function_id;
    std::optional<LabelUid> uid;
};

class LabelRegistry {
public:
    [[nodiscard]] bool add(
        const LabelUid& uid,
        const LabelDefinition& definition,
        LabelOrigin origin,
        LabelDiagnostic* diagnostic = nullptr);
    void freeze();

    [[nodiscard]] bool frozen() const noexcept { return frozen_; }
    [[nodiscard]] std::size_t size() const noexcept { return definitions_.size(); }
    [[nodiscard]] std::optional<LabelId> find_uid(const LabelUid& uid) const noexcept;
    [[nodiscard]] const LabelDefinition* find_definition(LabelId id) const noexcept;
    [[nodiscard]] const std::vector<LabelOrigin>* origins(const LabelUid& uid) const noexcept;
    [[nodiscard]] std::uint64_t semantic_fingerprint() const noexcept;

private:
    struct PendingLabel {
        LabelDefinition definition;
        std::vector<LabelOrigin> origins;
    };

    bool frozen_ = false;
    std::map<LabelUid, PendingLabel> pending_;
    std::map<LabelUid, LabelId> by_uid_;
    std::vector<LabelDefinition> definitions_;
};

class FunctionLabelTable {
public:
    [[nodiscard]] std::optional<LabelId> resolve(const LabelUid& uid) const noexcept;
    [[nodiscard]] bool visible(const LabelUid& uid) const noexcept;

private:
    friend class LabelLinker;
    std::map<LabelUid, LabelId> by_uid_;
};

using LabelFunctionLibrary = std::unordered_map<FunctionId, FunctionDescriptor>;
using FunctionLabelDeclarations = std::unordered_map<FunctionId, std::vector<LabelDeclaration>>;

struct LinkedLabels {
    LabelRegistry registry;
    std::unordered_map<FunctionId, FunctionLabelTable> function_tables;
    std::vector<LabelDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class LabelLinker {
public:
    [[nodiscard]] LinkedLabels link(
        const FunctionDescriptor& root,
        const LabelFunctionLibrary& functions,
        const FunctionLabelDeclarations& declarations) const;
};

[[nodiscard]] std::string to_string(LabelDiagnosticCode code);

} // namespace phoenix
