// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/dispatch.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/version.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
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

bool matches(const ProcessRequest& request, const methods::IlluminationReport& report) {
    if (!report.complete || report.status == methods::SurfaceStatus::failed) {
        return false;
    }
    const auto* const selected = std::get_if<methods::Surface>(&request.illumination());
    if (selected == nullptr) {
        return report.status == methods::SurfaceStatus::disabled && !report.requested;
    }
    return report.requested && *report.requested == selected->parameters() &&
           report.status != methods::SurfaceStatus::disabled;
}

Outcome process(const contract::Invocation& invocation, Processor& processor,
                const core::Cancellation& cancellation) {
    auto request = prepare_process(invocation);
    if (!request) {
        return failure(invocation, std::move(request.error()));
    }
    if (cancellation.requested(core::Checkpoint::admission)) {
        return failure(invocation, core::cancelled().error());
    }
    // Prepare a complete response before effects: even bad_alloc after commit must not make the
    // CLI report not_started. Returning this fallback during unwinding does not allocate.
    static_assert(std::is_nothrow_move_constructible_v<Outcome>);
    auto unknown_outcome =
        failure(invocation, {
                                .code = core::ErrorCode::publication_unknown,
                                .message = "Processing did not report its outcome; "
                                           "inspect the output before retrying",
                                .publication = core::Publication::unknown,
                            });
    try {
        auto result = processor.process(*request, cancellation);
        if (!result) {
            return succeeded(invocation, std::move(result.error()));
        }
        if (const auto* const method = std::get_if<methods::Binarization>(&request->operation())) {
            if (result->conversion || result->illumination) {
                return unknown_outcome;
            }
            return succeeded(invocation, Processed{
                                             .output = std::move(result->output),
                                             .method = methods::describe(*method),
                                         });
        }
        if (!result->conversion || !result->conversion->verified || !result->illumination ||
            !matches(*request, *result->illumination)) {
            return unknown_outcome;
        }
        return succeeded(invocation, ContinuousProcessed{
                                         .output = std::move(result->output),
                                         .conversion = *result->conversion,
                                         .illumination = *result->illumination,
                                     });
    } catch (...) {
        return unknown_outcome;
    }
}
} // namespace
Outcome failure(const contract::Invocation& invocation, core::Error error) {
    return {
        .command = invocation.command,
        .build = build_facts(),
        .payload = Failure{.error = std::move(error)},
    };
}
Outcome dispatch(const contract::Invocation& invocation, Processor& processor,
                 const core::Cancellation& cancellation) {
    const bool bare_root =
        invocation.command == contract::Command::root && !invocation.root_version;
    if (invocation.help || bare_root) {
        // The root command is the only one that lists the other commands.
        return succeeded(invocation,
                         Help{.list_commands = invocation.command == contract::Command::root});
    }
    if (invocation.command == contract::Command::process) {
        return process(invocation, processor, cancellation);
    }
    if (invocation.command == contract::Command::version || invocation.root_version) {
        auto outcome = succeeded(invocation, Version{.capabilities = capabilities()});
        outcome.command = contract::Command::version;
        return outcome;
    }
    if (invocation.command == contract::Command::methods) {
        auto available = capabilities();
        if (!invocation.subject.empty()) {
            const auto selected = std::ranges::find(available.methods, invocation.subject,
                                                    &methods::ImplementedMethod::id);
            if (selected == available.methods.end()) {
                return unavailable(invocation, "No completed method with this ID is available; "
                                               "see docs/methods.md for planned methods");
            }
            available.methods = available.methods.subspan(
                static_cast<std::size_t>(selected - available.methods.begin()), 1);
        }
        return succeeded(invocation, Methods{.capabilities = available});
    }
    return unavailable(invocation, std::string(contract::command_name(invocation.command)) +
                                       " is specified but not implemented; no inputs were "
                                       "opened and no outputs were created");
}
} // namespace docenhance::app
