// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/report/render.hpp"

#include "docenhance/app/dispatch.hpp"
#include "docenhance/contract/cli_contract.hpp"
#include "docenhance/contract/command.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
namespace docenhance::report {
namespace {
using Json = nlohmann::ordered_json;
constexpr int json_indent = 2;
// The envelope version of schemas/foundation-result.schema.json, which the CLI contract test
// validates every response against.
constexpr int schema_version = 1;

// Common fields first, then the payload's own fields in their given order.
Json envelope(const app::Outcome& outcome, const Json& fields) {
    Json json = {
        {"schema_version", schema_version},
        {"command", contract::command_name(outcome.command)},
        {"development_stage", outcome.build.development_stage},
        {"version", outcome.build.version},
        {"exit_code", static_cast<int>(outcome.exit_code)},
    };
    json.update(fields);
    return json;
}
// Messages can quote arguments, which need not be UTF-8. Replace invalid sequences with U+FFFD so
// the output is always valid JSON instead of a serialization exception.
std::string dump(const Json& json) {
    return json.dump(json_indent, ' ', false, Json::error_handler_t::replace) + "\n";
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
    std::string text = "DocEnhance " + std::string(outcome.build.version) +
                       " — development foundation\n" +
                       std::string(contract::command_usage(outcome.command)) + "\n\n";
    text += "Implemented: help, version, honest capability discovery.\n";
    text +=
        "Not implemented: image I/O, planning, presets and processing. No files are modified.\n\n";
    if (help.list_commands) {
        text += "Commands: process, plan, inspect, presets, methods, version\n\n";
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
                           " (foundation; image processing unavailable)\n",
                    .err = {},
                };
            } else if constexpr (std::is_same_v<Payload, app::Methods>) {
                return {
                    .out = "No complete image-processing methods are implemented in this "
                           "foundation.\n",
                    .err = {},
                };
            } else {
                return {
                    .out = {},
                    .err = std::string(payload.error.identifier()) + ": " + payload.error.message +
                           "\n",
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
                    {"contract_status", contract::contract_status},
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
            } else {
                const Json error = {
                    {"code", payload.error.identifier()},
                    {"message", payload.error.message},
                };
                const Json fields = {
                    {"error", error},
                    {"publication", "not_started"},
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
