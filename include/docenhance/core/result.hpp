// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace docenhance::core {
enum class ExitCode : int {
    success = 0,
    invocation = 2,
    input = 3,
    processing = 4,
    output = 5,
    partial = 6,
    publication_unknown = 7,
    invariant = 8,
    cancelled = 130,
};
// resource: a legitimate request the machine or the budget cannot satisfy.
enum class ErrorCode { argument, resource, unavailable, invariant };
struct Error {
    ErrorCode code;
    std::string message;
    [[nodiscard]] constexpr ExitCode exit_code() const noexcept {
        switch (code) {
        case ErrorCode::argument:
            return ExitCode::invocation;
        case ErrorCode::resource:
        case ErrorCode::unavailable:
            return ExitCode::processing;
        case ErrorCode::invariant:
            return ExitCode::invariant;
        }
        return ExitCode::invariant;
    }
    [[nodiscard]] constexpr std::string_view identifier() const noexcept {
        switch (code) {
        case ErrorCode::argument:
            return "E_ARGUMENT";
        case ErrorCode::resource:
            return "E_RESOURCE";
        case ErrorCode::unavailable:
            return "E_NOT_IMPLEMENTED";
        case ErrorCode::invariant:
            return "E_INVARIANT";
        }
        return "E_INVARIANT";
    }
};
// The standard value-or-error vocabulary type, carrying this project's Error. Callers must check:
// every function returning one is [[nodiscard]], and std::expected has no implicit value access.
template <typename T> using Result = std::expected<T, Error>;
// Spelled out so a failure reads as a failure at the call site.
[[nodiscard]] inline std::unexpected<Error> failure(ErrorCode code, std::string message) {
    return std::unexpected(Error{.code = code, .message = std::move(message)});
}
} // namespace docenhance::core
