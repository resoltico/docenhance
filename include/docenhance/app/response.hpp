// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/dispatch.hpp"
#include "docenhance/core/warnings.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace docenhance::app {
inline constexpr unsigned response_schema_version = 15U;
inline constexpr unsigned response_confirmed_limit = 16U;

// These owned wire values are made during rendering, after the effect outcome is retained.
struct ResponseParameter {
    std::string option, kind, domain, description;
    bool operator==(const ResponseParameter&) const = default;
};
struct ResponseMethod {
    std::string id;
    unsigned method_version{};
    std::string name, family;
    std::vector<std::string> selectors;
    std::vector<ResponseParameter> parameters;
    bool operator==(const ResponseMethod&) const = default;
};
struct ResponseInput {
    std::string format;
    std::vector<std::string> output_modes;
    bool operator==(const ResponseInput&) const = default;
};
struct ResponseCapabilities {
    std::vector<ResponseMethod> methods;
    std::vector<std::string> supported_formats;
    std::vector<ResponseInput> input_support;
    bool operator==(const ResponseCapabilities&) const = default;
};
struct ResponseOption {
    std::string name, metavar, description, domain, methods;
    bool operator==(const ResponseOption&) const = default;
};
struct ResponseHelp {
    std::string usage;
    std::vector<ResponseOption> options;
};
struct ResponseVersion : ResponseCapabilities {
    std::string dependency_lock_sha256, platform, compiler;
};
struct ResponsePublished {
    core::Publication publication = core::Publication::completed;
    std::string output;
    std::string record_sha256;
    record::RunRecord record;
    Diagnostics diagnostics;
    std::vector<core::Warning> warnings;
};
struct ResponseConfirmed {
    std::string path, sha256;
    std::uint64_t bytes{};
};
struct ResponseVerified {
    std::string directory, run, recorded;
    std::vector<ResponseConfirmed> confirmed;
    std::string establishes = "artifacts_agree_with_record";
};
struct ResponseError {
    core::ErrorCode code = core::ErrorCode::invariant;
    std::string message;
};
struct ResponseFailure {
    ResponseError error;
    core::Publication publication = core::Publication::not_started;
    std::optional<methods::Family> failed_stage;
    methods::CompletedStages completed;
    std::optional<Diagnostics> diagnostics;
    std::vector<core::Warning> warnings;
};
// Every alternative has the existing command discriminator and a closed concrete payload.
template <Command CommandValue, typename PayloadValue> struct Response : PayloadValue {
    unsigned schema_version = response_schema_version;
    std::string version;
    int exit_code{};
};
using WireResponse = std::variant<
    Response<Command::root, ResponseHelp>, Response<Command::process, ResponseHelp>,
    Response<Command::verify, ResponseHelp>, Response<Command::methods, ResponseHelp>,
    Response<Command::version, ResponseHelp>, Response<Command::version, ResponseVersion>,
    Response<Command::methods, ResponseCapabilities>, Response<Command::process, ResponsePublished>,
    Response<Command::verify, ResponseVerified>, Response<Command::root, ResponseFailure>,
    Response<Command::process, ResponseFailure>, Response<Command::verify, ResponseFailure>,
    Response<Command::methods, ResponseFailure>, Response<Command::version, ResponseFailure>>;

[[nodiscard]] bool validate(const ResponseCapabilities& value);
[[nodiscard]] bool validate(const ResponseVersion& value);
[[nodiscard]] bool validate(const ResponsePublished& value);
[[nodiscard]] bool validate(const ResponseVerified& value);
[[nodiscard]] bool validate(const ResponseFailure& value);
[[nodiscard]] bool valid_response_help(const ResponseHelp& value, Command command);

template <Command CommandValue, typename PayloadValue>
[[nodiscard]] bool validate(const Response<CommandValue, PayloadValue>& value) {
    if constexpr (std::is_same_v<PayloadValue, ResponseFailure>) {
        if constexpr (CommandValue != Command::process) {
            const auto& facts = value.completed;
            if (value.publication != core::Publication::not_started || value.failed_stage ||
                value.diagnostics || !value.warnings.empty() || facts.illumination ||
                facts.denoising || facts.restoration || facts.contrast || facts.sharpening ||
                facts.binarization) {
                return false;
            }
        }
        return value.exit_code ==
                   static_cast<int>(
                       core::Error{.code = value.error.code, .message = {}}.exit_code()) &&
               validate(static_cast<const ResponseFailure&>(value));
    } else if constexpr (std::is_same_v<PayloadValue, ResponseHelp>) {
        return value.exit_code == 0 && valid_response_help(value, CommandValue);
    } else {
        return value.exit_code == 0 && validate(static_cast<const PayloadValue&>(value));
    }
}
} // namespace docenhance::app
