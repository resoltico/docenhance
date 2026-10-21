// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"

#include <optional>

namespace docenhance::tests {
class RejectingProcessor final : public app::Processor {
  public:
    unsigned calls = 0;
    std::optional<app::ExecutionControl> observed;
    [[nodiscard]] app::ProcessResult process(const app::ProcessRequest& /*request*/,
                                             const app::ExecutionControl& control) override {
        ++calls;
        observed = control;
        return app::process_failure(
            {.code = core::ErrorCode::not_implemented, .message = "No I/O processor in this test"},
            core::Publication::not_started);
    }
};
} // namespace docenhance::tests
