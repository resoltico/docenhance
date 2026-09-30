// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/host/verifier.hpp"

#include "bundle_verify.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"

#include <expected>
#include <utility>

namespace docenhance::host {
core::Result<app::Verified> Verifier::verify(const app::VerifyRequest& request,
                                             const core::Cancellation& cancellation) {
    if (cancellation.requested(core::Checkpoint::admission)) {
        return core::cancelled();
    }
    auto checked = verify_bundle(request.directory(), cancellation);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    auto& declared = checked->declared;
    return app::Verified{
        .directory = request.directory(),
        .run = std::move(declared.run),
        .recorded = std::move(declared.recorded),
        .confirmed = std::move(declared.inventory),
    };
}
} // namespace docenhance::host
