#pragma once

#include <string>
#include <utility>

using partition_model_view = std::pair<const void*, const void*>;

// Source-compatible replacement for the production debug_json/dj plumbing.
// Partition threads this object through many trusted algorithm signatures, but
// it contributes only visualization and trace output. Keeping a null object
// avoids invasive signature edits and intentionally drops the legacy VM/debug
// dependency graph.
class null_partition_diagnostics {
public:
    enum flags { COLLAPSED = 1 };
    explicit constexpr null_partition_diagnostics(bool = false) noexcept {}

    constexpr void set_current() const noexcept {}
    constexpr void set_enabled(bool) const noexcept {}
    [[nodiscard]] constexpr bool enabled() const noexcept { return false; }
    constexpr void draw_all() const noexcept {}
    constexpr void set_flags(int) const noexcept {}

    template <typename T>
    [[nodiscard]] static std::string toString(const T&) { return {}; }

    [[nodiscard]] constexpr null_partition_diagnostics begin() const noexcept {
        return null_partition_diagnostics(false);
    }

    template <typename T>
    [[nodiscard]] constexpr null_partition_diagnostics begin(const T&) const noexcept {
        return null_partition_diagnostics(false);
    }

    [[nodiscard]] null_partition_diagnostics begin_array(
        const std::string& = {}) const noexcept {
        return null_partition_diagnostics(false);
    }

    template <typename... T>
    constexpr null_partition_diagnostics& add(T&&...) noexcept {
        return *this;
    }

    constexpr null_partition_diagnostics& add_type(const std::string&) noexcept {
        return *this;
    }

    constexpr void error(const std::string&) const noexcept {}
    constexpr void text(const std::string&) const noexcept {}
};

using debug_json = null_partition_diagnostics;
using dj = null_partition_diagnostics;
