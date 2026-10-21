// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/range.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>

namespace docenhance::core {
struct Count {};
struct Unit {};
struct Text {};
struct Instant {};
struct BundlePath {};
struct Literal {
    std::string_view text;
};
inline constexpr std::string_view text_pattern = R"(^[^\u0000]+$)";
struct Hex {
    std::size_t digits;
};
template <typename T> struct Exact {
    static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>);
    T value;
    template <typename Value> [[nodiscard]] constexpr bool contains(Value actual) const noexcept {
        static_assert(std::is_same_v<Value, T>);
        return actual == value;
    }
};

template <typename T, std::size_t Size> struct Choices {
    static_assert(Size != 0);
    static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>);
    std::array<T, Size> values;
    template <typename Value> [[nodiscard]] constexpr bool contains(Value actual) const noexcept {
        static_assert(std::is_same_v<Value, T>);
        return std::ranges::any_of(values, [actual](T value) noexcept { return actual == value; });
    }
};
template <typename T, std::size_t Size> Choices(std::array<T, Size>) -> Choices<T, Size>;

template <typename Domain> struct ScalarChoice {
    Domain numeric;
    std::string_view empty;
};

template <typename Owner, typename Member, typename Domain = std::monostate> struct Field {
    std::string_view key;
    Member Owner::* member;
    Domain domain{};
};

template <typename Owner, typename Member, typename Domain = std::monostate>
[[nodiscard]] constexpr Field<Owner, Member, Domain>
field(std::string_view key, Member Owner::* member, Domain domain = {}) {
    return {.key = key, .member = member, .domain = domain};
}

template <typename T> struct Fields;
template <typename T> struct EnumNames;
// Each alternative owns its literal discriminator; unrelated unions may use different keys.
template <typename T> struct WireTag;
template <typename T>
inline constexpr bool has_wire_tag = requires {
    WireTag<T>::key;
    WireTag<T>::value;
};
template <typename T> inline constexpr bool null_value = std::is_same_v<T, std::monostate>;
// Only explicitly declared sparse objects may omit absent optional fields.
template <typename T> inline constexpr bool sparse_fields = false;

template <typename T> [[nodiscard]] consteval bool valid_field_keys() {
    constexpr auto keys = std::apply(
        [](auto... item) { return std::array<std::string_view, sizeof...(item)>{item.key...}; },
        Fields<T>::list);
    constexpr bool members =
        std::apply([](auto... item) { return ((item.member != nullptr) && ...); }, Fields<T>::list);
    if (!members) {
        return false;
    }
    for (std::size_t i = 0; i < keys.size(); ++i) {
        for (std::size_t j = i + 1; j < keys.size(); ++j) {
            if (keys.at(i) == keys.at(j)) {
                return false;
            }
        }
        if constexpr (has_wire_tag<T>) {
            if (keys.at(i) == WireTag<T>::key) {
                return false;
            }
        }
    }
    return true;
}
} // namespace docenhance::core
