// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <type_traits>

namespace docenhance::core {
template <typename T>
    requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
[[nodiscard]] constexpr bool in_range(T value, T minimum, T maximum, bool odd = false) noexcept {
    if (!(value >= minimum && value <= maximum)) {
        return false;
    }
    if constexpr (std::is_integral_v<T>) {
        return !odd || value % 2 != 0;
    } else {
        return true;
    }
}

template <typename T> struct Range {
    static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>);
    T minimum;
    T maximum;
    bool odd = false;
    [[nodiscard]] constexpr bool contains(T value) const noexcept {
        return in_range(value, minimum, maximum, odd);
    }
};
} // namespace docenhance::core
