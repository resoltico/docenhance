// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/dispatch.hpp"

#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/capabilities.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/version.hpp"

#include <string>
#include <utility>
namespace docenhance::app {
namespace {
BuildFacts build_facts() noexcept {
    return {
        .version = application_version,
        .platform = build_platform,
        .compiler = build_compiler,
        .dependency_lock_sha256 = dependency_lock_sha256,
    };
}
// Reported, never assumed: these are the lists the layers below actually implement.
Capabilities capabilities() noexcept {
    return {
        .methods = methods::implemented_methods(),
        .input_formats = io::supported_input_formats(),
    };
}
Outcome succeeded(const contract::Invocation& invocation, Payload payload) {
    return {
        .command = invocation.command,
        .exit_code = core::ExitCode::success,
        .build = build_facts(),
        .payload = std::move(payload),
    };
}
Outcome unavailable(const contract::Invocation& invocation, std::string message) {
    return failure(invocation,
                   {.code = core::ErrorCode::unavailable, .message = std::move(message)});
}
} // namespace
Outcome failure(const contract::Invocation& invocation, core::Error error) {
    return {
        .command = invocation.command,
        .exit_code = error.exit_code(),
        .build = build_facts(),
        .payload = Failure{.error = std::move(error)},
    };
}
Outcome dispatch(const contract::Invocation& invocation) {
    const bool bare_root =
        invocation.command == contract::Command::root && !invocation.root_version;
    if (invocation.help || bare_root) {
        // The root command is the only one that lists the other commands.
        return succeeded(invocation,
                         Help{.list_commands = invocation.command == contract::Command::root});
    }
    const bool targets_input = invocation.command == contract::Command::process ||
                               invocation.command == contract::Command::plan ||
                               invocation.command == contract::Command::inspect;
    if (targets_input && invocation.subject.empty()) {
        return failure(invocation,
                       {.code = core::ErrorCode::argument, .message = "INPUT is required"});
    }
    if (invocation.command == contract::Command::process && invocation.output_directory.empty()) {
        return failure(invocation,
                       {.code = core::ErrorCode::argument, .message = "--out-dir is required"});
    }
    if (invocation.command == contract::Command::version || invocation.root_version) {
        return succeeded(invocation, Version{.capabilities = capabilities()});
    }
    if (invocation.command == contract::Command::methods) {
        if (!invocation.subject.empty()) {
            return unavailable(invocation, "No completed method with this ID is available; "
                                           "see docs/methods.md for planned methods");
        }
        return succeeded(invocation, Methods{.capabilities = capabilities()});
    }
    return unavailable(invocation, std::string(contract::command_name(invocation.command)) +
                                       " is specified but not implemented; no inputs were "
                                       "opened and no outputs were created");
}
} // namespace docenhance::app
