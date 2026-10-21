// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/report/response.hpp"

#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/response.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "docenhance/app/response_fields.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/core/warnings.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/report/warnings.hpp"

#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace docenhance::report {
core::Result<record::Json> encode_response(const app::WireResponse& response) {
    return record::Codec::encode(response);
}
app::ResponseCapabilities response_capabilities(const app::Capabilities& value) {
    app::ResponseCapabilities result;
    for (const auto& method : value.methods) {
        app::ResponseMethod owned{
            .id = std::string{method.id},
            .method_version = method.method_version,
            .name = std::string{method.name},
            .family = std::string{method.family},
            .selectors = std::vector<std::string>(),
            .parameters = std::vector<app::ResponseParameter>(),
        };
        for (const auto selector : method.selectors) {
            owned.selectors.emplace_back(selector);
        }
        for (const auto& parameter : method.parameters) {
            owned.parameters.push_back({
                .option = std::string{parameter.option},
                .kind = std::string{parameter.kind},
                .domain = std::string{parameter.domain},
                .description = std::string{parameter.description},
            });
        }
        result.methods.push_back(std::move(owned));
    }
    for (const auto& input : value.input_support) {
        result.supported_formats.emplace_back(input.format);
        app::ResponseInput support{
            .format = std::string{input.format},
            .output_modes = {"preserve", "gray"},
        };
        if (input.binary) {
            support.output_modes.emplace_back("bw");
        }
        result.input_support.push_back(std::move(support));
    }
    return result;
}
namespace {
template <typename Payload>
core::Result<app::WireResponse> envelope(app::Command command, const core::BuildFacts& build,
                                         Payload payload, int exit) {
    const auto tagged = [&]<app::Command Value> -> app::WireResponse {
        app::Response<Value, Payload> result;
        result.version = std::string{build.version};
        result.exit_code = exit;
        static_cast<Payload&>(result) = std::move(payload);
        return result;
    };
    if constexpr (std::is_same_v<Payload, app::ResponseHelp> ||
                  std::is_same_v<Payload, app::ResponseFailure>) {
        switch (command) {
        case app::Command::root:
            return tagged.template operator()<app::Command::root>();
        case app::Command::process:
            return tagged.template operator()<app::Command::process>();
        case app::Command::verify:
            return tagged.template operator()<app::Command::verify>();
        case app::Command::methods:
            return tagged.template operator()<app::Command::methods>();
        case app::Command::version:
            return tagged.template operator()<app::Command::version>();
        }
    } else if constexpr (std::is_same_v<Payload, app::ResponsePublished>) {
        if (command == app::Command::process) {
            return tagged.template operator()<app::Command::process>();
        }
    } else if constexpr (std::is_same_v<Payload, app::ResponseVerified>) {
        if (command == app::Command::verify) {
            return tagged.template operator()<app::Command::verify>();
        }
    } else if constexpr (std::is_same_v<Payload, app::ResponseVersion>) {
        if (command == app::Command::version) {
            return tagged.template operator()<app::Command::version>();
        }
    } else {
        static_assert(std::is_same_v<Payload, app::ResponseCapabilities>);
        if (command == app::Command::methods) {
            return tagged.template operator()<app::Command::methods>();
        }
    }
    return core::failure(core::ErrorCode::invariant, "Outcome command does not match its payload");
}
app::ResponseHelp help_value(app::Command command) {
    app::ResponseHelp result{
        .usage = std::string{app::command_usage(command)},
        .options = std::vector<app::ResponseOption>(),
    };
    for (const auto& option : app::option_catalog) {
        if (option.scope.contains(command)) {
            result.options.push_back({
                .name = std::string{option.name},
                .metavar = std::string{option.metavar},
                .description = std::string{option.description},
                .domain = std::string{option.domain},
                .methods = std::string{option.methods},
            });
        }
    }
    return result;
}
std::vector<core::Warning> warning_values(const RunWarnings& source) {
    std::vector<core::Warning> result;
    for (const auto warning : source) {
        if (warning) {
            result.push_back(*warning);
        }
    }
    return result;
}
app::ResponseFailure failure_value(const app::ProcessFailure& value) {
    std::string message = value.error.message;
    if (message.empty()) {
        message = value.error.code == core::ErrorCode::resource ? "The system refused an allocation"
                                                                : "No diagnostic was reported";
    } else if (!core::valid_utf8(message)) {
        message = "The diagnostic was not well-formed UTF-8";
    } else if (message.contains('\0')) {
        message = "The diagnostic contained a NUL character";
    }
    return {
        .error = {.code = value.error.code, .message = std::move(message)},
        .publication = value.publication,
        .failed_stage = value.failed_stage,
        .completed = value.completed,
        .diagnostics = value.diagnostics,
        .warnings = warning_values(failure_warnings(value)),
    };
}
app::ResponseVerified verified_value(const app::Verified& value) {
    app::ResponseVerified result{
        .directory = value.directory,
        .run = value.run,
        .recorded = value.recorded,
        .confirmed = std::vector<app::ResponseConfirmed>(),
    };
    for (const auto& entry : value.confirmed) {
        result.confirmed.push_back(
            {.path = entry.name, .sha256 = entry.identity.sha256, .bytes = entry.identity.bytes});
    }
    return result;
}
} // namespace
core::Result<app::WireResponse> response_value(const app::Outcome& outcome) {
    return std::visit(
        [&](const auto& payload) -> core::Result<app::WireResponse> {
            using Value = std::decay_t<decltype(payload)>;
            const auto finish = [&](auto value) {
                return envelope(outcome.command, outcome.build, std::move(value),
                                static_cast<int>(outcome.exit_code()));
            };
            if constexpr (std::is_same_v<Value, app::Help>) {
                return finish(help_value(outcome.command));
            } else if constexpr (std::is_same_v<Value, app::Version>) {
                app::ResponseVersion value;
                static_cast<app::ResponseCapabilities&>(value) =
                    response_capabilities(payload.capabilities);
                value.dependency_lock_sha256 = outcome.build.dependency_lock_sha256;
                value.platform = outcome.build.platform;
                value.compiler = outcome.build.compiler;
                return finish(std::move(value));
            } else if constexpr (std::is_same_v<Value, app::Methods>) {
                return finish(response_capabilities(payload.capabilities));
            } else if constexpr (std::is_same_v<Value, app::Published>) {
                return finish(app::ResponsePublished{
                    .output = payload.output,
                    .record_sha256 = payload.record_identity.sha256,
                    .record = payload.record,
                    .diagnostics = payload.diagnostics,
                    .warnings = warning_values(run_warnings(payload.record)),
                });
            } else if constexpr (std::is_same_v<Value, app::Verified>) {
                return finish(verified_value(payload));
            } else {
                return finish(failure_value(payload));
            }
        },
        outcome.payload);
}
} // namespace docenhance::report
