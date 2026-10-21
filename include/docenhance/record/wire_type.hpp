// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

namespace docenhance::record {
enum class WireKind { value, optional, sequence, variant };
template <typename T> struct WireType {
    static constexpr WireKind kind = WireKind::value;
};
template <typename T> struct WireType<std::optional<T>> {
    static constexpr WireKind kind = WireKind::optional;
    using Element = T;
};
template <typename T, typename Allocator> struct WireType<std::vector<T, Allocator>> {
    static constexpr WireKind kind = WireKind::sequence;
    static constexpr bool fixed = false;
    using Element = T;
};
template <typename T, std::size_t Size> struct WireType<std::array<T, Size>> {
    static constexpr WireKind kind = WireKind::sequence;
    static constexpr bool fixed = true;
    static constexpr std::size_t size = Size;
    using Element = T;
};
template <typename... T> struct WireType<std::variant<T...>> {
    static constexpr WireKind kind = WireKind::variant;
};
} // namespace docenhance::record
