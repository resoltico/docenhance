// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/contract/cli_contract.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/catalog.hpp"
#include "observations.hpp"

#include <algorithm>
#include <cstddef>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::app {
namespace {
// Reported, never assumed: these are the lists the layers below actually implement.
Capabilities capabilities() noexcept {
    return {
        .methods = methods::implemented_methods(),
        .input_support = contract::input_support,
    };
}
Outcome succeeded(const contract::Invocation& invocation, Payload payload) {
    return {
        .command = invocation.command,
        .build = core::build_facts(),
        .payload = std::move(payload),
    };
}
Outcome not_implemented(const contract::Invocation& invocation, std::string message) {
    return failure(invocation,
                   {.code = core::ErrorCode::not_implemented, .message = std::move(message)});
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
            return valid_failure(result.error(), *request)
                       ? succeeded(invocation, std::move(result.error()))
                       : std::move(unknown_outcome);
        }
        if (const auto* const method = std::get_if<methods::Binarization>(&request->operation())) {
            auto* const image = std::get_if<PublishedBinary>(&*result);
            if (image == nullptr || !valid_published(*image, *request)) {
                return unknown_outcome;
            }
            return succeeded(invocation, Processed{
                                             .output = std::move(image->output),
                                             .method = methods::describe(*method),
                                             .run = std::move(image->run),
                                             .record = std::move(image->record),
                                             .source_decoding = image->source_decoding,
                                             .otsu = image->otsu,
                                             .rotation = image->rotation,
                                         });
        }
        auto* const image = std::get_if<PublishedContinuous>(&*result);
        if (image == nullptr || !valid_published(*image, *request)) {
            return unknown_outcome;
        }
        return succeeded(invocation, std::move(*image));
    } catch (...) {
        return unknown_outcome;
    }
}
} // namespace
Outcome failure(const contract::Invocation& invocation, core::Error error) {
    return {
        .command = invocation.command,
        .build = core::build_facts(),
        .payload = Failure{.error = std::move(error)},
    };
}
namespace {
Outcome verify(const contract::Invocation& invocation, Verifier& verifier,
               const core::Cancellation& cancellation) {
    auto request = prepare_verify(invocation);
    if (!request) {
        return failure(invocation, std::move(request.error()));
    }
    if (cancellation.requested(core::Checkpoint::admission)) {
        return failure(invocation, core::cancelled().error());
    }
    // Read-only execution cannot publish. Prepare fallbacks before opening anything so catches
    // and malformed returns need no diagnostic allocation after the port starts.
    auto invalid =
        failure(invocation, {
                                .code = core::ErrorCode::invariant,
                                .message = "Verification returned inconsistent observations",
                            });
    auto exhausted = failure(invocation, {
                                             .code = core::ErrorCode::resource,
                                             .message = "Verification exhausted a system resource",
                                         });
    try {
        auto verified = verifier.verify(*request, cancellation);
        if (!verified) {
            const auto& error = verified.error();
            if (!error.valid_publication() || error.publication != core::Publication::not_started ||
                error.code == core::ErrorCode::output_verify ||
                error.code == core::ErrorCode::method_inapplicable ||
                error.code == core::ErrorCode::numerical) {
                return invalid;
            }
            return failure(invocation, std::move(verified.error()));
        }
        return valid_verified(*verified, *request) ? succeeded(invocation, std::move(*verified))
                                                   : std::move(invalid);
    } catch (const std::bad_alloc&) {
        return exhausted;
    } catch (...) {
        return invalid;
    }
}
} // namespace

Outcome dispatch(const contract::Invocation& invocation, Processor& processor, Verifier& verifier,
                 const core::Cancellation& cancellation) {
    if (!contract::CommandSet::all().contains(invocation.command)) {
        auto refused = failure(
            invocation, {.code = core::ErrorCode::argument, .message = "Unknown command value"});
        refused.command = contract::Command::root;
        return refused;
    }
    const bool bare_root =
        invocation.command == contract::Command::root && !invocation.root_version;
    if (invocation.help || bare_root) {
        // The root command is the only one that lists the other commands.
        return succeeded(invocation,
                         Help{
                             .list_commands = invocation.command == contract::Command::root,
                             .capabilities = capabilities(),
                         });
    }
    if (invocation.command == contract::Command::process) {
        return process(invocation, processor, cancellation);
    }
    if (invocation.command == contract::Command::verify) {
        return verify(invocation, verifier, cancellation);
    }
    if (invocation.command == contract::Command::version || invocation.root_version) {
        auto outcome = succeeded(invocation, Version{
                                                 .capabilities = capabilities(),
                                             });
        outcome.command = contract::Command::version;
        return outcome;
    }
    if (invocation.command == contract::Command::methods) {
        auto available = capabilities();
        if (!invocation.subject.empty()) {
            const auto selected = std::ranges::find(available.methods, invocation.subject,
                                                    &methods::ImplementedMethod::id);
            if (selected == available.methods.end()) {
                return not_implemented(invocation, "No completed method with this ID is available; "
                                                   "see docs/methods.md for planned methods");
            }
            available.methods = available.methods.subspan(
                static_cast<std::size_t>(selected - available.methods.begin()), 1);
        }
        return succeeded(invocation, Methods{.capabilities = available});
    }
    return not_implemented(invocation, std::string(contract::command_name(invocation.command)) +
                                           " is specified but not implemented; no inputs were "
                                           "opened and no outputs were created");
}
} // namespace docenhance::app
