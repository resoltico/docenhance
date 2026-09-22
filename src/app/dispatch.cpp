// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/dispatch.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/version.hpp"

#include <array>
#include <string>
#include <string_view>
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
    static constexpr auto formats = std::to_array<std::string_view>({"png"});
    return {
        .methods = methods::implemented_methods(),
        .input_formats = formats,
    };
}
Outcome succeeded(const contract::Invocation& invocation, Payload payload) {
    return {
        .command = invocation.command,
        .build = build_facts(),
        .payload = std::move(payload),
    };
}
Outcome unavailable(const contract::Invocation& invocation, std::string message) {
    return failure(invocation,
                   {.code = core::ErrorCode::unavailable, .message = std::move(message)});
}

Outcome process(const contract::Invocation& invocation, Processor& processor) {
    auto request = prepare_process(invocation);
    if (!request) {
        return failure(invocation, std::move(request.error()));
    }
    auto result = processor.process(*request);
    return result ? succeeded(invocation, std::move(*result))
                  : failure(invocation, std::move(result.error()));
}
} // namespace
Outcome failure(const contract::Invocation& invocation, core::Error error) {
    return {
        .command = invocation.command,
        .build = build_facts(),
        .payload = Failure{.error = std::move(error)},
    };
}
Outcome dispatch(const contract::Invocation& invocation, Processor& processor) {
    const bool bare_root =
        invocation.command == contract::Command::root && !invocation.root_version;
    if (invocation.help || bare_root) {
        // The root command is the only one that lists the other commands.
        return succeeded(invocation,
                         Help{.list_commands = invocation.command == contract::Command::root});
    }
    if (invocation.command == contract::Command::process) {
        return process(invocation, processor);
    }
    if (invocation.command == contract::Command::version || invocation.root_version) {
        auto outcome = succeeded(invocation, Version{.capabilities = capabilities()});
        outcome.command = contract::Command::version;
        return outcome;
    }
    if (invocation.command == contract::Command::methods) {
        if (!invocation.subject.empty() && invocation.subject != "B03") {
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
