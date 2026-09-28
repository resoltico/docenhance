// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/verify.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"

namespace docenhance::tests {
// For the cases that exercise everything except reading a bundle back. A test that reaches this
// is asking the wrong port, and says so.
class UnusedVerifier final : public app::Verifier {
  public:
    [[nodiscard]] core::Result<app::Verified>
    verify(const app::VerifyRequest& /*request*/,
           const core::Cancellation& /*cancellation*/) override {
        return core::failure(core::ErrorCode::invariant, "This case does not verify a bundle");
    }
};
} // namespace docenhance::tests
