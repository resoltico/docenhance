// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/report/render.hpp"

#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/contract/cli_contract.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/contract/utf8.hpp"
#include "docenhance/core/result.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::report {
namespace {
using Json = nlohmann::ordered_json;
constexpr int json_indent = 2;
// The envelope version of schemas/command-response.schema.json, which the CLI contract test
// validates every response against.
constexpr int schema_version = 1;

std::string_view publication_name(core::Publication publication) noexcept {
    switch (publication) {
    case core::Publication::not_started:
        return "not_started";
    case core::Publication::not_published:
        return "not_published";
    case core::Publication::completed:
        return "completed";
    case core::Publication::unknown:
        return "unknown";
    }
    return "unknown";
}

// Common fields first, then the payload's own fields in their given order.
Json envelope(const app::Outcome& outcome, const Json& fields) {
    Json json = {
        {"schema_version", schema_version},
        {"command", contract::command_name(outcome.command)},
        {"version", outcome.build.version},
        {"exit_code", static_cast<int>(outcome.exit_code())},
    };
    json.update(fields);
    return json;
}
// Identity fields must never be silently repaired. Diagnostic text alone may be substituted;
// malformed identities cause serialization failure, contained by the transport boundary.
std::string dump(const Json& json) {
    return json.dump(json_indent) + "\n";
}
std::string_view diagnostic(const core::Error& error) noexcept {
    return contract::valid_utf8(error.message) ? std::string_view{error.message}
                                               : "The diagnostic was not well-formed UTF-8";
}
Json capability_fields(const app::Capabilities& capabilities) {
    Json methods = Json::array();
    for (const auto& method : capabilities.methods) {
        methods.push_back({{"id", method.id}, {"method_version", method.method_version}});
    }
    Json formats = Json::array();
    for (const auto format : capabilities.input_formats) {
        formats.push_back(format);
    }
    return {{"methods", methods}, {"supported_formats", formats}};
}
Json option_fields(contract::Command command) {
    Json options = Json::array();
    for (const auto& option : contract::option_catalog) {
        if (!option.scope.contains(command)) {
            continue;
        }
        options.push_back({
            {"name", option.name},
            {"metavar", option.metavar},
            {"description", option.description},
            {"domain", option.domain},
            {"methods", option.methods},
        });
    }
    return options;
}
std::string help_text(const app::Outcome& outcome, const app::Help& help) {
    std::string text = "DocEnhance " + std::string(outcome.build.version) + "\n" +
                       std::string(contract::command_usage(outcome.command)) + "\n\n";
    text += "Implemented: 1/2/4/8-bit grayscale PNG input and 8-bit B02 Sauvola or B03 "
            "fixed-threshold output.\n";
    text += "Not implemented: other image formats, color handling, batching, presets, and other "
            "methods.\n\n";
    if (help.list_commands) {
        text += "Commands: process, methods, version\n\n";
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
        text += "\n    ";
        text += option.description;
        text += '\n';
    }
    return text;
}
Output text_form(const app::Outcome& outcome) {
    return std::visit(
        [&outcome](const auto& payload) -> Output {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, app::Help>) {
                return {.out = help_text(outcome, payload), .err = {}};
            } else if constexpr (std::is_same_v<Payload, app::Version>) {
                return {
                    .out = "DocEnhance " + std::string(outcome.build.version) +
                           " (grayscale PNG binarization)\n",
                    .err = {},
                };
            } else if constexpr (std::is_same_v<Payload, app::Methods>) {
                std::string text;
                for (const auto& method : payload.capabilities.methods) {
                    text += std::string(method.id) + " " + std::string(method.selector) + "\n";
                }
                return {.out = std::move(text), .err = {}};
            } else if constexpr (std::is_same_v<Payload, app::Processed>) {
                return {.out = "Wrote " + payload.output + "\n", .err = {}};
            } else {
                return {
                    .out = {},
                    .err = std::string(payload.error.identifier()) + ": " +
                           std::string(diagnostic(payload.error)) + "\n",
                };
            }
        },
        outcome.payload);
}
Output json_form(const app::Outcome& outcome) {
    return std::visit(
        [&outcome](const auto& payload) -> Output {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, app::Help>) {
                const Json fields = {
                    {"usage", contract::command_usage(outcome.command)},
                    {"options", option_fields(outcome.command)},
                };
                return {.out = dump(envelope(outcome, fields)), .err = {}};
            } else if constexpr (std::is_same_v<Payload, app::Version>) {
                Json fields = {
                    {"dependency_lock_sha256", outcome.build.dependency_lock_sha256},
                    {"platform", outcome.build.platform},
                    {"compiler", outcome.build.compiler},
                };
                fields.update(capability_fields(payload.capabilities));
                return {.out = dump(envelope(outcome, fields)), .err = {}};
            } else if constexpr (std::is_same_v<Payload, app::Methods>) {
                const auto fields = capability_fields(payload.capabilities);
                return {.out = dump(envelope(outcome, fields)), .err = {}};
            } else if constexpr (std::is_same_v<Payload, app::Processed>) {
                const Json fields = {
                    {"method", payload.method.id},
                    {"method_version", payload.method.method_version},
                    {"output", payload.output},
                    {"publication", "completed"},
                };
                return {.out = dump(envelope(outcome, fields)), .err = {}};
            } else {
                const Json error = {
                    {"code", payload.error.identifier()},
                    {"message", diagnostic(payload.error)},
                };
                const Json fields = {
                    {"error", error},
                    {"publication", publication_name(payload.error.publication)},
                };
                return {.out = dump(envelope(outcome, fields)), .err = {}};
            }
        },
        outcome.payload);
}
} // namespace
Output render(const app::Outcome& outcome, Format format) {
    return format == Format::json ? json_form(outcome) : text_form(outcome);
}
} // namespace docenhance::report
