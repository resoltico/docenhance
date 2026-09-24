// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/catalog.hpp"

#include <span>
#include <string_view>
#include <variant>
namespace docenhance::app {
// What an invocation produced, in types. Nothing here is formatted: how an outcome reaches a
// person or another program is the report layer's decision, and this layer never makes it.

// The identity of this build, as the build system recorded it.
struct BuildFacts {
    std::string_view version;
    std::string_view platform;
    std::string_view compiler;
    std::string_view dependency_lock_sha256;
};
// Capabilities admitted by the application and verified by real end-to-end tests.
struct Capabilities {
    std::span<const methods::ImplementedMethod> methods;
    std::span<const std::string_view> input_formats;
};
struct Help {
    bool list_commands = false;
};
struct Version {
    Capabilities capabilities;
};
struct Methods {
    Capabilities capabilities;
};
struct Failure {
    core::Error error;
};
using Payload = std::variant<Help, Version, Methods, Processed, Failure>;
struct Outcome {
    contract::Command command = contract::Command::root;
    // Every response identifies the build that produced it, whatever the payload.
    BuildFacts build;
    Payload payload;
    [[nodiscard]] core::ExitCode exit_code() const noexcept {
        const auto* const failed = std::get_if<Failure>(&payload);
        return failed == nullptr ? core::ExitCode::success : failed->error.exit_code();
    }
};
[[nodiscard]] Outcome dispatch(const contract::Invocation& invocation, Processor& processor,
                               const core::Cancellation& cancellation = {});
[[nodiscard]] Outcome failure(const contract::Invocation& invocation, core::Error error);
} // namespace docenhance::app
