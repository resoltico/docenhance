// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/verify.hpp"

#include "admission.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"

#include <expected>
#include <utility>
namespace docenhance::app {
core::Result<VerifyRequest> prepare_verify(const Invocation& invocation) {
    if (invocation.command != Command::verify) {
        return core::failure(core::ErrorCode::argument, "Expected a verify invocation");
    }
    if (auto checked = validate_invocation(invocation); !checked) {
        return std::unexpected(std::move(checked.error()));
    }
    if (invocation.operands.empty() ||
        invocation.operands.size() < command_operands(invocation.command).minimum) {
        return core::failure(core::ErrorCode::argument, "DIRECTORY is required");
    }
    const auto& directory = invocation.operands.front();
    if (!core::valid_path(directory)) {
        return core::failure(core::ErrorCode::argument, "Paths must be valid UTF-8 without NUL");
    }
    return VerifyRequest{directory};
}
} // namespace docenhance::app
