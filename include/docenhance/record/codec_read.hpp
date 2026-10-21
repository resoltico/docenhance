// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/scalar.hpp"
#include "docenhance/record/wire_type.hpp"

#include <cstddef>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace docenhance::record::wire_read {
template <typename T> [[nodiscard]] bool read(const Json& input, T& output);
template <typename T> [[nodiscard]] bool read_sequence(const Json& input, T& output);
template <std::size_t Index = 0, typename T>
[[nodiscard]] bool read_variant(const Json& input, T& output);
template <typename T> [[nodiscard]] bool read_object(const Json& input, T& output);
template <typename T, typename Member>
[[nodiscard]] bool read_member(const Json& input, T& output, const Member& member,
                               std::size_t& seen);
template <typename T, typename Domain>
[[nodiscard]] bool read_field(const Json& input, T& output, const Domain& domain);

template <typename T> [[nodiscard]] bool valid_object(const T& value) {
    if constexpr (requires { validate(value); }) {
        return static_cast<bool>(validate(value));
    } else {
        return true;
    }
}

template <typename T> [[nodiscard]] bool read(const Json& input, T& output) {
    if constexpr (core::null_value<T>) {
        return input.is_null();
    } else if constexpr (WireType<T>::kind == WireKind::optional) {
        if (input.is_null()) {
            output.reset();
            return true;
        }
        typename WireType<T>::Element value{};
        if (!wire_read::read(input, value)) {
            return false;
        }
        output = std::move(value);
        return true;
    } else if constexpr (WireType<T>::kind == WireKind::sequence) {
        return wire_read::read_sequence(input, output);
    } else if constexpr (WireType<T>::kind == WireKind::variant) {
        return wire_read::read_variant(input, output);
    } else if constexpr (requires { core::Fields<T>::list; }) {
        return wire_read::read_object(input, output);
    } else {
        return ScalarCodec::decode(input, output);
    }
}

template <typename T> [[nodiscard]] bool read_sequence(const Json& input, T& output) {
    if (!input.is_array()) {
        return false;
    }
    if constexpr (WireType<T>::fixed) {
        if (input.size() != output.size()) {
            return false;
        }
    } else {
        output.resize(input.size());
    }
    for (std::size_t i = 0; i < input.size(); ++i) {
        typename WireType<T>::Element value{};
        if (!wire_read::read(input.at(i), value)) {
            return false;
        }
        output.at(i) = std::move(value);
    }
    return true;
}

template <std::size_t Index, typename T>
[[nodiscard]] bool read_variant(const Json& input, T& output) {
    if constexpr (Index == std::variant_size_v<T>) {
        return false;
    } else {
        using Alternative = std::variant_alternative_t<Index, T>;
        static_assert(core::null_value<Alternative> || core::has_wire_tag<Alternative>);
        Alternative value{};
        if (wire_read::read(input, value)) {
            output.template emplace<Index>(std::move(value));
            return true;
        }
        return wire_read::read_variant<Index + 1>(input, output);
    }
}

template <typename T> [[nodiscard]] bool read_object(const Json& input, T& output) {
    static_assert(core::valid_field_keys<T>());
    if (!input.is_object()) {
        return false;
    }
    std::size_t seen = 0;
    if constexpr (core::has_wire_tag<T>) {
        const auto tag = input.find(core::WireTag<T>::key);
        if (tag == input.end() || !tag->is_string() ||
            tag->template get_ref<const Json::string_t&>() != core::WireTag<T>::value) {
            return false;
        }
        ++seen;
    }
    bool valid = true;
    std::apply(
        [&](auto... item) {
            ((valid = valid && wire_read::read_member(input, output, item, seen)), ...);
        },
        core::Fields<T>::list);
    return valid && seen == input.size() && wire_read::valid_object(output);
}

template <typename T, typename Member>
[[nodiscard]] bool read_member(const Json& input, T& output, const Member& member,
                               std::size_t& seen) {
    auto& field = output.*member.member;
    using Value = std::remove_cvref_t<decltype(field)>;
    const auto value = input.find(member.key);
    if (value == input.end()) {
        if constexpr (core::sparse_fields<T> && WireType<Value>::kind == WireKind::optional) {
            field.reset();
            return true;
        }
        return false;
    }
    ++seen;
    if constexpr (core::sparse_fields<T>) {
        if (value->is_null()) {
            return false;
        }
    }
    return wire_read::read_field(*value, field, member.domain);
}

template <typename T, typename Domain>
[[nodiscard]] bool read_field(const Json& input, T& output, const Domain& domain) {
    if constexpr (requires {
                      domain.numeric;
                      domain.empty;
                  }) {
        static_assert(WireType<T>::kind == WireKind::optional);
        if (input.is_string()) {
            if (input.template get_ref<const Json::string_t&>() != domain.empty) {
                return false;
            }
            output.reset();
            return true;
        }
        typename WireType<T>::Element value{};
        if (!wire_read::read_field(input, value, domain.numeric)) {
            return false;
        }
        output = std::move(value);
        return true;
    } else if constexpr (WireType<T>::kind == WireKind::optional) {
        if (input.is_null()) {
            output.reset();
            return true;
        }
        typename WireType<T>::Element value{};
        if (!wire_read::read_field(input, value, domain)) {
            return false;
        }
        output = std::move(value);
        return true;
    } else if constexpr (WireType<T>::kind == WireKind::sequence &&
                         std::is_same_v<Domain, core::Range<std::size_t>>) {
        return input.is_array() && domain.contains(input.size()) && wire_read::read(input, output);
    } else {
        return wire_read::read(input, output) && ScalarCodec::valid(output, domain);
    }
}
} // namespace docenhance::record::wire_read
