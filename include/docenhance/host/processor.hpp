// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"

namespace docenhance::host {
// The run identity and the instant a record carries. Both are environment inputs, taken once
// per admitted execution through a composition-root source; tests can pin that source.
[[nodiscard]] bundle::RunContext observed_context();

// Composition-root implementation; never linked by pure application tests or CLI fuzzers.
// The source supplies one identity/instant per admitted execution, before any filesystem effects.
class Processor final : public app::Processor {
  public:
    explicit Processor(bundle::RunContext (&context_source)() = observed_context)
        : context_source_(&context_source) {}
    [[nodiscard]] app::ProcessResult process(const app::ProcessRequest& request,
                                             const core::Cancellation& cancellation) override;

  private:
    bundle::RunContext (*context_source_)();
};
} // namespace docenhance::host
