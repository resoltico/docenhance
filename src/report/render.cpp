// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/report/render.hpp"

#include "continuous.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/report/response.hpp"
#include "docenhance/report/warnings.hpp"
#include "help.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::report {
namespace {
constexpr int json_indent = 2;
std::string publication_text(core::Publication publication) {
    std::string text = "Publication: ";
    switch (publication) {
    case core::Publication::not_started:
        text += "not_started\nPublication did not start; correct the reported problem before "
                "retrying.\n";
        break;
    case core::Publication::not_published:
        text += "not_published\nNo result was published; correct the reported problem before "
                "retrying.\n";
        break;
    case core::Publication::completed:
        text +=
            "completed\nA result was published; inspect the destination and use 'docenhance verify "
            "DIRECTORY' before taking further action. Do not retry blindly.\n";
        break;
    case core::Publication::unknown:
        text +=
            "unknown\nEffects are unconfirmed; inspect the destination and staging entries before "
            "taking further action. Do not retry blindly.\n";
        break;
    }
    return text + "An incomplete response or delivery failure does not prove absence of effects.\n";
}
std::string_view diagnostic(const core::Error& error) noexcept {
    if (error.message.empty()) {
        return error.code == core::ErrorCode::resource ? "The system refused an allocation"
                                                       : "No diagnostic was reported";
    }
    if (error.message.contains('\0')) {
        return "The diagnostic contained a NUL character";
    }
    return core::valid_utf8(error.message) ? std::string_view{error.message}
                                           : "The diagnostic was not well-formed UTF-8";
}
std::string verified_text(const app::Verified& value) {
    std::string text = "Bundle agrees with its record: " + value.directory + "\n";
    text += "run " + value.run + " recorded " + value.recorded + "\n";
    for (const auto& artifact : value.confirmed) {
        text += artifact.name + " " + artifact.identity.sha256 + "\n";
    }
    return text + "Agreement is not authenticity: a digest detects disagreement with "
                  "expected bytes, it does not identify who produced them.\n";
}
core::Result<Output> failure_text(const app::ProcessFailure& payload) {
    auto completed = completed_text(payload.completed);
    if (!completed) {
        return std::unexpected(std::move(completed.error()));
    }
    core::Result<std::string> diagnostics{std::string{}};
    if (payload.diagnostics) {
        diagnostics = diagnostics_text(*payload.diagnostics);
    }
    if (!diagnostics) {
        return std::unexpected(std::move(diagnostics.error()));
    }
    auto warnings = warnings_text(failure_warnings(payload));
    if (!warnings) {
        return std::unexpected(std::move(warnings.error()));
    }
    return Output{
        .out = {},
        .err = std::string(payload.error.identifier()) + ": " +
               std::string(diagnostic(payload.error)) + "\n" +
               publication_text(payload.publication) + *completed + *diagnostics + *warnings,
    };
}
core::Result<Output> text_form(const app::Outcome& outcome) {
    return std::visit(
        [&](const auto& payload) -> core::Result<Output> {
            using Value = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Value, app::Help>) {
                return Output{.out = help_text(outcome, payload), .err = {}};
            } else if constexpr (std::is_same_v<Value, app::Version>) {
                return Output{
                    .out = "DocEnhance " + std::string(outcome.build.version) + "\n",
                    .err = {},
                };
            } else if constexpr (std::is_same_v<Value, app::Methods>) {
                return Output{.out = methods_text(payload.capabilities.methods), .err = {}};
            } else if constexpr (std::is_same_v<Value, app::Published>) {
                auto text = continuous_text(payload);
                if (!text) {
                    return std::unexpected(std::move(text.error()));
                }
                return Output{.out = std::move(*text), .err = {}};
            } else if constexpr (std::is_same_v<Value, app::Verified>) {
                return Output{.out = verified_text(payload), .err = {}};
            } else {
                return failure_text(payload);
            }
        },
        outcome.payload);
}
} // namespace
core::Result<Output> render(const app::Outcome& outcome, Format format) {
    if (format == Format::json) {
        auto value = response_value(outcome);
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        auto encoded = encode_response(*value);
        if (!encoded) {
            return std::unexpected(std::move(encoded.error()));
        }
        return Output{.out = encoded->dump(json_indent) + "\n", .err = {}};
    }
    return text_form(outcome);
}
} // namespace docenhance::report
