// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"

namespace docenhance::tests {
class RejectingProcessor final : public app::Processor {
  public:
    unsigned calls = 0;
    [[nodiscard]] app::ProcessResult process(const app::ProcessRequest& /*request*/,
                                             const core::Cancellation& /*cancellation*/) override {
        ++calls;
        return app::process_failure(
            {.code = core::ErrorCode::unavailable, .message = "No I/O processor in this test"});
    }
};
} // namespace docenhance::tests
