// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/host/verifier.hpp"

#include "docenhance/app/verify.hpp"
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/bundle.hpp"

#include <expected>
#include <utility>

namespace docenhance::host {
core::Result<app::Verified> Verifier::verify(const app::VerifyRequest& request,
                                             const core::Cancellation& cancellation) {
    if (cancellation.requested(core::Checkpoint::admission)) {
        return core::cancelled();
    }
    // The record has its own bound. Verification processes nothing, so it never opens a working
    // budget: this one exists to hold the record and nothing else.
    core::Budget budget{bundle::record_max_bytes};
    auto bytes = io::read_bundle_file(request.directory(), bundle::record_name,
                                      bundle::record_max_bytes, budget);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto declared = bundle::read_record(bytes->bytes());
    if (!declared) {
        return std::unexpected(declared.error());
    }
    if (cancellation.requested(core::Checkpoint::verification)) {
        return core::cancelled();
    }
    auto contents = io::inspect_bundle(request.directory());
    if (!contents) {
        return std::unexpected(contents.error());
    }
    auto agreed = bundle::agrees(*declared, contents->files, contents->directories);
    if (!agreed) {
        return std::unexpected(agreed.error());
    }
    return app::Verified{
        .directory = request.directory(),
        .run = std::move(declared->run),
        .recorded = std::move(declared->recorded),
        .confirmed = std::move(declared->inventory),
    };
}
} // namespace docenhance::host
