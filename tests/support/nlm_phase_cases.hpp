// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/parameters.hpp"
#include "gamma_fixture.hpp"
#include "nlm_working_cases.hpp"
#include "nlm_working_fixture.hpp"
#include "require.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
namespace docenhance::tests {
inline std::size_t nlm_phase_visits(core::Checkpoint phase) {
    NlmWorkingFixture fixture{7, 7, nlm_working_test_scratch};
    const CheckpointStop trace{phase, std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    trace.cancellation()};
    const auto result = run_nlm(fixture, {.h = 0.1, .patch = 3, .search = 7}, scheduler);
    require(result.has_value(), "healthy actual NLM phase counterpart calibrates real visits");
    return CheckpointStop::visits();
}
inline void nlm_phase_stop(core::Checkpoint phase, std::size_t allowed) {
    NlmWorkingFixture fixture{7, 7, nlm_working_test_scratch};
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto held = fixture.budget.used();
    const CheckpointStop stop{phase, allowed};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = run_nlm(fixture, {.h = 0.1, .patch = 3, .search = 7}, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == allowed + 1 && fixture.budget.used() == held,
            "real phase stop returns no complete Facts and refunds all joined NLM owners");
    if (phase == core::Checkpoint::initialization || phase == core::Checkpoint::measurement ||
        phase == core::Checkpoint::allocation) {
        require(gamma_bits(fixture.luminance.view().storage()) == before,
                "these actual single-band premap acquisition/gather/initialization stops preserve "
                "full Y");
    }
}
inline void nlm_phase_cases() {
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::initialization,
             core::Checkpoint::measurement,
             core::Checkpoint::processing,
             core::Checkpoint::scheduling,
         }) {
        const auto visits = nlm_phase_visits(phase);
        require(visits > 3, "each required actual NLM phase is meaningfully reached");
        for (const auto allowed : {std::size_t{0}, std::size_t{1}, std::size_t{3}, visits - 1}) {
            nlm_phase_stop(phase, allowed);
        }
    }
    NlmWorkingFixture wide{2051, 7, nlm_working_test_scratch};
    const auto before = gamma_bits(wide.luminance.view().storage());
    const auto held = wide.budget.used();
    const CheckpointStop stop{core::Checkpoint::processing, 1};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = run_nlm(wide, {.h = 0.1, .patch = 3, .search = 7}, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == 2 && wide.budget.used() == held &&
                gamma_bits(wide.luminance.view().storage()) == before,
            "actual wide difference row stops at an interior256-sample checkpoint before mapping");
}
} // namespace docenhance::tests
