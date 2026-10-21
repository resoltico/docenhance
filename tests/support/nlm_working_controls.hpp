// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "gamma_fixture.hpp"
#include "nlm_numerical_cases.hpp"
#include "nlm_phase_cases.hpp"
#include "nlm_working_cases.hpp"
#include "nlm_working_fixture.hpp"
#include "require.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
namespace docenhance::tests {
inline void nlm_exact_resource_cases() {
    for (const auto extra : {std::size_t{7936}, std::size_t{7935}}) {
        NlmWorkingFixture fixture{7, 7, extra};
        for (std::uint32_t y = 0; y < 7; ++y) {
            std::ranges::fill(fixture.luminance.view().row(y), 0.5);
        }
        const auto held = fixture.budget.used();
        const auto before = gamma_bits(fixture.luminance.view().storage());
        const CheckpointStop observe{core::Checkpoint::allocation,
                                     std::numeric_limits<std::size_t>::max()};
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                        observe.cancellation()};
        const auto result = run_nlm(fixture, {.h = 0.1, .patch = 3, .search = 7}, scheduler);
        require(CheckpointStop::visits() == 9 && fixture.budget.used() == held,
                "actual five shared and four band acquisitions are reached and all owners refund");
        if (extra == 7936) {
            require(result && result->reason == methods::NlmReason::no_effect &&
                        result->counts.evaluated == 49 && result->corrected_samples == 0 &&
                        result->weight_table_entries == 694 &&
                        gamma_bits(fixture.luminance.view().storage()) == before &&
                        fixture.budget.peak() == held + extra,
                    "real typed uint32 method owners attain the reviewed7936-byte peak and active "
                    "constant identity");
        } else {
            require(!result && result.error().code == core::ErrorCode::resource &&
                        fixture.budget.peak() == held + 7872 &&
                        gamma_bits(fixture.luminance.view().storage()) == before,
                    "one-byte-short actual last columns acquisition refuses before this single "
                    "band's map");
        }
    }
}
inline void nlm_noop_cases() {
    NlmWorkingFixture fixture{1, 441, 0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto held = fixture.budget.used();
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> mask_before(mask.begin(), mask.end());
    const auto zero = run_nlm(fixture, {.blend = 0}, scheduler);
    require(
        zero && zero->reason == methods::NlmReason::zero_blend &&
            gamma_bits(fixture.luminance.view().storage()) == before &&
            std::ranges::equal(mask, mask_before) && fixture.budget.used() == held &&
            fixture.budget.peak() == held,
        "actual tiny-axis zero blend precedes active search without algorithm scratch or stores");
    const methods::NlmParameters defaults;
    const auto active = run_nlm(fixture, defaults, scheduler);
    require(
        !active && active.error().code == core::ErrorCode::method_inapplicable &&
            gamma_bits(fixture.luminance.view().storage()) == before &&
            std::ranges::equal(mask, mask_before) && fixture.budget.used() == held &&
            fixture.budget.peak() == held,
        "actual two-axis refusal preserves all Y/mask bytes and performs no algorithm allocation");
    std::ranges::fill(fixture.protection.view().storage(), 255);
    const std::vector<std::uint8_t> protected_before(mask.begin(), mask.end());
    const auto both = run_nlm(fixture, {.blend = 0}, scheduler);
    require(both && both->reason == methods::NlmReason::zero_blend &&
                gamma_bits(fixture.luminance.view().storage()) == before &&
                std::ranges::equal(mask, protected_before) && fixture.budget.used() == held &&
                fixture.budget.peak() == held,
            "blend0 before E0 immediately preserves the intentionally filled protected mask and "
            "all Y bits");
    const auto empty = run_nlm(fixture, defaults, scheduler);
    require(
        empty && empty->reason == methods::NlmReason::no_eligible_samples &&
            gamma_bits(fixture.luminance.view().storage()) == before &&
            std::ranges::equal(mask, protected_before) && fixture.budget.used() == held &&
            fixture.budget.peak() == held,
        "E0 immediately preserves full Y/mask payload and bypasses active search with no scratch");
}
inline void nlm_reducer_stop_case() {
    NlmWorkingFixture healthy{7, 7, nlm_working_test_scratch};
    NlmWorkingFixture stopped{7, 7, nlm_working_test_scratch};
    const auto before = gamma_bits(healthy.luminance.view().storage());
    const methods::NlmParameters p{.h = 1, .patch = 3, .search = 7};
    std::size_t visits = 0;
    {
        const CheckpointStop trace{core::Checkpoint::processing,
                                   std::numeric_limits<std::size_t>::max()};
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                        trace.cancellation()};
        const auto result = run_nlm(healthy, p, scheduler);
        require(result && result->counts.changed > 0,
                "healthy actual NLM map attains nonidentity before reducer stop calibration");
        visits = CheckpointStop::visits();
    }
    require(visits > 1,
            "actual processing trace includes mapping and the final postjoin reducer poll");
    const auto after = gamma_bits(healthy.luminance.view().storage());
    const auto pitch = healthy.luminance.view().row_pitch();
    bool changed_tail = false;
    for (std::size_t x = 0; x < 7; ++x) {
        changed_tail = changed_tail || before.at((6 * pitch) + x) != after.at((6 * pitch) + x);
    }
    require(changed_tail,
            "healthy last map row actually WOULD differ if the prior map poll cancelled instead");
    const auto held = stopped.budget.used();
    const CheckpointStop stop{core::Checkpoint::processing, visits - 1};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = run_nlm(stopped, p, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == visits && stopped.budget.used() == held &&
                gamma_bits(stopped.luminance.view().storage()) == after,
            "actual postjoin reducer stop returns no Facts after full private mapping and refunds "
            "every owner");
}
inline void nlm_working_control_cases() {
    nlm_working_cases();
    nlm_exact_resource_cases();
    nlm_noop_cases();
    nlm_reducer_stop_case();
    nlm_constant_code_cases();
    nlm_border_and_mean_cases();
    nlm_decoded_equal_case();
    nlm_phase_cases();
}
} // namespace docenhance::tests
