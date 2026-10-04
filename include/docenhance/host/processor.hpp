// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"

#include <utility>

namespace docenhance::host {
// The run identity and the instant a record carries. Both are environment inputs, taken once
// here so that nothing below the composition root reads a clock or an entropy source and a test
// can pin them.
[[nodiscard]] bundle::RunContext observed_context();

// Composition-root implementation; never linked by pure application tests or CLI fuzzers.
class Processor final : public app::Processor {
  public:
    explicit Processor(bundle::RunContext context = observed_context())
        : context_(std::move(context)) {}
    [[nodiscard]] app::ProcessResult process(const app::ProcessRequest& request,
                                             const core::Cancellation& cancellation) override;

  private:
    bundle::RunContext context_;
};
} // namespace docenhance::host
