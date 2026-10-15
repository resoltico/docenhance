// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/invocation.hpp"

#include <expected>
#include <span>
#include <string>
#include <string_view>
namespace docenhance::cli {
struct SyntaxError {
    app::Command command = app::Command::root;
    std::string message;
};
// Arguments exclude the program name. Syntax consumes values verbatim and checks only the
// maximum operand count; application admission owns missing operands and option domains.
[[nodiscard]] std::expected<app::Invocation, SyntaxError>
tokenize(std::span<const std::string_view> arguments);
// Only syntax failure uses this raw exact-token fallback; successful syntax uses parsed roles.
template <typename Token> [[nodiscard]] bool syntax_json(std::span<Token> arguments) noexcept {
    for (const auto token : arguments) {
        const std::string_view value{token};
        if (value == "--") {
            return false;
        }
        if (value == "--json") {
            return true;
        }
    }
    return false;
}
} // namespace docenhance::cli
