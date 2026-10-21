// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"

#include <string>
#include <tuple>

namespace docenhance::tests {
struct WireStrings {
    std::string text = "caf\xc3\xa9";
    std::string instant = "2024-02-29T23:59:59Z";
    std::string path = "assets/caf\xc3\xa9.png";
    std::string literal = "agreement";
    bool operator==(const WireStrings&) const = default;
};
struct WireEmptyLiteral {
    std::string text;
    bool operator==(const WireEmptyLiteral&) const = default;
};
} // namespace docenhance::tests
namespace docenhance {
template <> struct core::Fields<tests::WireStrings> {
    using Value = tests::WireStrings;
    static constexpr auto list = std::tuple{
        core::field("text", &Value::text, core::Text{}),
        core::field("instant", &Value::instant, core::Instant{}),
        core::field("path", &Value::path, core::BundlePath{}),
        core::field("literal", &Value::literal, core::Literal{.text = "agreement"}),
    };
};
template <> struct core::Fields<tests::WireEmptyLiteral> {
    static constexpr auto list = std::tuple{
        core::field("text", &tests::WireEmptyLiteral::text, core::Literal{.text = ""}),
    };
};
} // namespace docenhance
