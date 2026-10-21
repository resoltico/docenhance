// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/host/verifier.hpp"

#include "bundle_verify.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/record/inventory.hpp"
#include "docenhance/record/record.hpp"

#include <expected>
#include <utility>

namespace docenhance::host {
core::Result<app::Verified> Verifier::verify(const app::VerifyRequest& request,
                                             const core::Cancellation& cancellation) {
    if (!request.ready()) {
        return core::failure(core::ErrorCode::argument,
                             "Verification request no longer owns its admitted path");
    }

    if (cancellation.requested(core::Checkpoint::admission)) {
        return core::cancelled();
    }
    auto checked = verify_bundle(request.directory(), cancellation);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    auto& declared = checked->declared;
    auto inventory = record::inventory(declared);
    if (!inventory) {
        return std::unexpected(std::move(inventory.error()));
    }
    inventory->push_back({.name = record::record_name, .identity = std::move(checked->record)});
    return app::Verified{
        .directory = request.directory(),
        .run = std::move(declared.context.identity),
        .recorded = std::move(declared.context.recorded),
        .confirmed = std::move(*inventory),
    };
}
} // namespace docenhance::host
