// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"

#include "admission.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/catalog.hpp"

#include <algorithm>
#include <cstddef>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
namespace docenhance::app {
namespace {
// Reported, never assumed: these are the lists the layers below actually implement.
Capabilities capabilities() noexcept {
    return {
        .methods = methods::implemented_methods(),
        .input_support = app::input_support,
    };
}
Outcome succeeded(const app::Invocation& invocation, Payload payload) {
    return {
        .command = invocation.command,
        .build = core::build_facts(),
        .payload = std::move(payload),
    };
}
Outcome not_implemented(const app::Invocation& invocation, std::string message) {
    return failure(invocation,
                   {.code = core::ErrorCode::not_implemented, .message = std::move(message)});
}

Outcome process(const app::Invocation& invocation, Processor& processor,
                const core::Cancellation& cancellation) {
    auto request = prepare_process(invocation);
    if (!request) {
        return failure(invocation, std::move(request.error()));
    }
    auto control = prepare_execution(invocation, cancellation);
    if (!control) {
        return failure(invocation, std::move(control.error()));
    }
    if (cancellation.requested(core::Checkpoint::admission)) {
        return failure(invocation, core::cancelled().error());
    }
    // Prepare a complete response before effects: even bad_alloc after commit must not make the
    // CLI report not_started. Returning this fallback during unwinding does not allocate.
    static_assert(std::is_nothrow_move_constructible_v<Outcome>);
    auto unknown_outcome = failure(invocation,
                                   {
                                       .code = core::ErrorCode::publication_unknown,
                                       .message = "Processing did not report its outcome; "
                                                  "inspect the output before retrying",
                                   },
                                   core::Publication::unknown);
    try {
        auto result = processor.process(*request, *control);
        if (!result) {
            return succeeded(invocation, std::move(result.error()));
        }
        return succeeded(invocation, std::move(*result));
    } catch (...) {
        return unknown_outcome;
    }
}
} // namespace
Outcome failure(const app::Invocation& invocation, core::Error error,
                core::Publication publication) {
    return {
        .command = invocation.command,
        .build = core::build_facts(),
        .payload = Failure{.error = std::move(error), .publication = publication},
    };
}
namespace {
Outcome verify(const app::Invocation& invocation, Verifier& verifier,
               const core::Cancellation& cancellation) {
    auto request = prepare_verify(invocation);
    if (!request) {
        return failure(invocation, std::move(request.error()));
    }
    if (cancellation.requested(core::Checkpoint::admission)) {
        return failure(invocation, core::cancelled().error());
    }
    // Read-only execution cannot publish. Prepare fallbacks before opening anything so catches
    // need no diagnostic allocation while containing an execution exception.
    auto unexpected = failure(invocation, {
                                              .code = core::ErrorCode::invariant,
                                              .message = "Unexpected verification exception",
                                          });
    auto exhausted = failure(invocation, {
                                             .code = core::ErrorCode::resource,
                                             .message = "Verification exhausted a system resource",
                                         });
    try {
        auto verified = verifier.verify(*request, cancellation);
        if (!verified) {
            return failure(invocation, std::move(verified.error()));
        }
        return succeeded(invocation, std::move(*verified));
    } catch (const std::bad_alloc&) {
        return exhausted;
    } catch (...) {
        return unexpected;
    }
}
} // namespace

Outcome dispatch(const app::Invocation& invocation, Processor& processor, Verifier& verifier,
                 const core::Cancellation& cancellation) {
    if (auto checked = validate_invocation(invocation); !checked) {
        auto refused = failure(invocation, std::move(checked.error()));
        if (!CommandSet::all().contains(invocation.command)) {
            refused.command = Command::root;
        }
        return refused;
    }
    const bool bare_root =
        invocation.command == app::Command::root && !invocation.contains(OptionId::version);
    if (invocation.contains(OptionId::help) || bare_root) {
        // The root command is the only one that lists the other commands.
        return succeeded(invocation, Help{
                                         .list_commands = invocation.command == app::Command::root,
                                         .capabilities = capabilities(),
                                     });
    }
    if (invocation.command == app::Command::process) {
        return process(invocation, processor, cancellation);
    }
    if (invocation.command == app::Command::verify) {
        return verify(invocation, verifier, cancellation);
    }
    if (invocation.command == app::Command::version || invocation.contains(OptionId::version)) {
        auto outcome = succeeded(invocation, Version{
                                                 .capabilities = capabilities(),
                                             });
        outcome.command = app::Command::version;
        return outcome;
    }
    if (invocation.command == app::Command::methods) {
        auto available = capabilities();
        if (!invocation.operands.empty()) {
            const auto selected = std::ranges::find(available.methods, invocation.operands.front(),
                                                    &methods::ImplementedMethod::id);
            if (selected == available.methods.end()) {
                return not_implemented(invocation,
                                       "No implemented method with this ID is available; "
                                       "see docs/methods.md and docs/roadmap.md");
            }
            available.methods = available.methods.subspan(
                static_cast<std::size_t>(selected - available.methods.begin()), 1);
        }
        return succeeded(invocation, Methods{.capabilities = available});
    }
    return not_implemented(invocation, std::string(app::command_name(invocation.command)) +
                                           " is specified but not implemented; no inputs were "
                                           "opened and no outputs were created");
}
} // namespace docenhance::app
