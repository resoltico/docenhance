// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"

namespace docenhance::host {
// Composition-root implementation; never linked by pure application tests or CLI fuzzers.
class Processor final : public app::Processor {
  public:
    [[nodiscard]] core::Result<app::Processed> process(const app::ProcessRequest& request) override;
};
} // namespace docenhance::host
