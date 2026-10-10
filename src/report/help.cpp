// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "help.hpp"

#include "docenhance/app/dispatch.hpp"
#include "docenhance/contract/cli_contract.hpp"
#include "docenhance/contract/command.hpp"

#include <cstddef>
#include <string>
#include <string_view>
namespace docenhance::report {
namespace {
void append_wrapped(std::string& output, std::string_view body) {
    constexpr std::size_t width = 92;
    while (!body.empty()) {
        auto count = body.size();
        if (count > width) {
            count = body.rfind(' ', width);
            if (count == std::string_view::npos) {
                count = body.find(' ');
            }
            if (count == std::string_view::npos) {
                count = body.size();
            }
        }
        output += "    ";
        output += body.substr(0, count);
        output += '\n';
        body.remove_prefix(count);
        if (body.starts_with(' ')) {
            body.remove_prefix(1);
        }
    }
}
} // namespace
std::string help_text(const app::Outcome& outcome, const app::Help& help) {
    std::string text = "DocEnhance " + std::string(outcome.build.version) + "\n" +
                       std::string(contract::command_usage(outcome.command)) + "\n\n";
    text += "Implemented methods:";
    for (const auto& method : help.capabilities.methods) {
        text += " " + std::string(method.id) + " (" + std::string(method.selector) + ")";
    }
    text += "\nInput formats and output modes:\n";
    for (const auto& support : help.capabilities.input_support) {
        text += "  " + std::string(support.format) + ": preserve, gray";
        if (support.binary) {
            text += ", bw";
        }
        text += '\n';
    }
    text += '\n';
    if (help.list_commands) {
        text += "Commands: process, verify, methods, version\n\n";
    }
    for (const auto& option : contract::option_catalog) {
        if (!option.scope.contains(outcome.command)) {
            continue;
        }
        text += option.name;
        if (!option.metavar.empty()) {
            text += ' ';
            text += option.metavar;
        }
        text += '\n';
        append_wrapped(text, option.description);
        append_wrapped(text, "Default / domain: " + std::string(option.domain));
    }
    return text;
}
} // namespace docenhance::report
