// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"

namespace docenhance::tests {
class RejectingProcessor final : public app::Processor {
  public:
    unsigned calls = 0;
    [[nodiscard]] core::Result<app::PublishedImage>
    process(const app::ProcessRequest& /*request*/) override {
        ++calls;
        return core::failure(core::ErrorCode::unavailable, "No I/O processor in this test");
    }
};
} // namespace docenhance::tests
