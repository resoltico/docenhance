// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/verify.hpp"

#include "docenhance/contract/command.hpp"
#include "docenhance/contract/utf8.hpp"
#include "docenhance/core/result.hpp"

namespace docenhance::app {
core::Result<VerifyRequest> prepare_verify(const contract::Invocation& invocation) {
    if (invocation.command != contract::Command::verify) {
        return core::failure(core::ErrorCode::argument, "Expected a verify invocation");
    }
    if (invocation.subject.empty()) {
        return core::failure(core::ErrorCode::argument, "DIRECTORY is required");
    }
    if (invocation.subject.contains('\0')) {
        return core::failure(core::ErrorCode::argument, "Paths cannot contain NUL bytes");
    }
    if (!contract::valid_utf8(invocation.subject)) {
        return core::failure(core::ErrorCode::argument, "Paths must be well-formed UTF-8");
    }
    return VerifyRequest{invocation.subject};
}
} // namespace docenhance::app
