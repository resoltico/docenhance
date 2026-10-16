// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/cli/tokenize.hpp"

#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/core/utf8.hpp"

#include <algorithm>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
namespace docenhance::cli {
namespace {
using app::Command;
using app::Invocation;
using app::OptionId;
std::unexpected<SyntaxError> refused(Command command, std::string message) {
    return std::unexpected(SyntaxError{.command = command, .message = std::move(message)});
}
const app::OptionDescriptor* option_named(std::string_view name) {
    for (const auto& option : app::option_catalog) {
        if (option.name == name) {
            return &option;
        }
    }
    return nullptr;
}
std::expected<void, SyntaxError> assign_option(std::string_view token,
                                               std::span<const std::string_view>& remaining,
                                               Invocation& invocation) {
    const auto separator = token.find('=');
    const auto name = token.substr(0, separator);
    const auto* const found = option_named(name);
    if (found == nullptr || !found->scope.contains(invocation.command)) {
        return refused(invocation.command, "Unknown option: " + std::string(name));
    }
    if (invocation.contains(found->id)) {
        return refused(invocation.command, "Repeated option: " + std::string(name));
    }
    if (found->kind == app::ValueKind::flag) {
        if (separator != std::string_view::npos) {
            return refused(invocation.command, "A flag cannot have a value: " + std::string(name));
        }
        invocation.option(found->id).emplace();
        return {};
    }
    if (separator != std::string_view::npos) {
        invocation.option(found->id) = token.substr(separator + 1);
    } else if (!remaining.empty()) {
        invocation.option(found->id) = remaining.front();
        remaining = remaining.subspan(1);
    } else {
        return refused(invocation.command, "Missing option value: " + std::string(name));
    }
    return {};
}
Command command_for(std::string_view token) {
    for (unsigned value = 0; value < app::command_count; ++value) {
        const auto command = static_cast<Command>(value);
        if (command != Command::root && token == app::command_name(command)) {
            return command;
        }
    }
    return Command::root;
}
} // namespace
std::expected<app::Invocation, SyntaxError> tokenize(std::span<const std::string_view> arguments) {
    Invocation invocation;
    if (std::ranges::any_of(arguments, [](auto text) { return !core::valid_utf8(text); })) {
        return refused(Command::root, "Arguments must be well-formed UTF-8");
    }
    if (!arguments.empty()) {
        invocation.command = command_for(arguments.front());
        if (invocation.command != Command::root) {
            arguments = arguments.subspan(1);
        }
    }
    bool options_ended = false;
    while (!arguments.empty()) {
        const auto token = arguments.front();
        arguments = arguments.subspan(1);
        if (!options_ended && token == "--") {
            options_ended = true;
        } else if (!options_ended && token.starts_with("--")) {
            if (auto assigned = assign_option(token, arguments, invocation); !assigned) {
                return std::unexpected(std::move(assigned.error()));
            }
        } else {
            invocation.operands.emplace_back(token);
            if (invocation.operands.size() > app::command_operands(invocation.command).maximum) {
                return refused(invocation.command, "Unexpected additional operand");
            }
        }
    }
    if (invocation.command == Command::root && invocation.contains(OptionId::version) &&
        (invocation.contains(OptionId::help) || invocation.contains(OptionId::json))) {
        return refused(Command::root, "Use 'version --json'; --version must be used alone");
    }
    return invocation;
}
} // namespace docenhance::cli
