#pragma once

// Forced into the read-only production translation units by the compatibility
// probe. CGAL 6.2 no longer provides this declaration through the transitive
// include graph used by the production precompiled header.
#include <CGAL/Aos_observer.h>

#include <boost/variant/get.hpp>
#include <boost/variant.hpp>

#include <variant>

// CGAL 6 replaced several Boost.Variant result types with std::variant. The
// production algorithm consistently uses the pointer form of boost::get; this
// overload is a probe/port boundary bridge and preserves every call site.
namespace boost {
template <typename T, typename... Alternatives>
[[nodiscard]] constexpr T* get(std::variant<Alternatives...>* value) noexcept {
    return std::get_if<T>(value);
}

template <typename T, typename... Alternatives>
[[nodiscard]] constexpr const T* get(
    const std::variant<Alternatives...>* value) noexcept {
    return std::get_if<T>(value);
}
}  // namespace boost
