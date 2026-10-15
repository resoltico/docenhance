// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <span>
#include <string_view>
namespace docenhance::methods {
struct MethodParameter {
    std::string_view option;
    std::string_view kind;
    std::string_view domain;
    std::string_view description;
    constexpr bool operator==(const MethodParameter&) const = default;
};
struct ImplementedMethod {
    std::string_view id;
    unsigned method_version;
    std::string_view selector;
    std::string_view name;
    std::string_view family;
    std::span<const std::string_view> selectors;
    std::span<const MethodParameter> parameters;
    constexpr bool operator==(const ImplementedMethod& other) const {
        return id == other.id && method_version == other.method_version &&
               selector == other.selector && name == other.name && family == other.family &&
               std::ranges::equal(selectors, other.selectors) &&
               std::ranges::equal(parameters, other.parameters);
    }
};
// Only complete, validated algorithms belong here. Actual variant descriptors must equal the
// generated declarations at compile time; a declaration alone cannot create an implementation.
[[nodiscard]] std::span<const ImplementedMethod> implemented_methods() noexcept;
} // namespace docenhance::methods
