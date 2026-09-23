// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/process.hpp"

#include "docenhance/contract/command.hpp"
#include "docenhance/contract/parse.hpp"
#include "docenhance/contract/utf8.hpp"
#include "docenhance/core/result.hpp"

#include <string>
namespace docenhance::app {
core::Result<ProcessRequest> prepare_process(const contract::Invocation& invocation) {
    if (invocation.command != contract::Command::process) {
        return core::failure(core::ErrorCode::argument, "Expected a process invocation");
    }
    if (invocation.subject.empty()) {
        return core::failure(core::ErrorCode::argument, "INPUT is required");
    }
    if (invocation.output_directory.empty()) {
        return core::failure(core::ErrorCode::argument, "--out-dir is required");
    }
    if (invocation.subject.contains('\0') || invocation.output_directory.contains('\0')) {
        return core::failure(core::ErrorCode::argument, "Paths cannot contain NUL bytes");
    }
    if (!contract::valid_utf8(invocation.subject) ||
        !contract::valid_utf8(invocation.output_directory)) {
        return core::failure(core::ErrorCode::argument, "Paths must be well-formed UTF-8");
    }
    if (invocation.binarize != "fixed") {
        return core::failure(core::ErrorCode::argument,
                             "The only supported --binarize value is fixed");
    }
    constexpr double default_threshold = 0.5;
    double threshold = default_threshold;
    if (!invocation.fixed_threshold.empty()) {
        const auto parsed = contract::parse_finite(invocation.fixed_threshold, 0.0, 1.0);
        if (!parsed) {
            return core::failure(parsed.error().code, parsed.error().message);
        }
        threshold = *parsed;
    }
    return ProcessRequest{invocation.subject, invocation.output_directory, threshold};
}
} // namespace docenhance::app
