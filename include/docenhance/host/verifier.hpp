// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/verify.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"

namespace docenhance::host {
// Composition-root implementation of the read-only port. It reads a record within its bound,
// checks the bundle against what that record declares, and executes nothing it finds.
class Verifier final : public app::Verifier {
  public:
    [[nodiscard]] core::Result<app::Verified>
    verify(const app::VerifyRequest& request, const core::Cancellation& cancellation) override;
};
} // namespace docenhance::host
