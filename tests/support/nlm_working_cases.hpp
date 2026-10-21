// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/nlm_working.hpp"
#include "docenhance/methods/parameters.hpp"
#include "gamma_fixture.hpp"
#include "nlm_working_fixture.hpp"
#include "nlm_working_reference.hpp"
#include "require.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace docenhance::tests {
inline auto run_nlm(NlmWorkingFixture& fixture, methods::NlmParameters p,
                    const exec::Scheduler& scheduler, bool mask = true) {
    return methods::apply(
        methods::Nlm::create(p).value(),
        {fixture.luminance.view(),
         mask ? fixture.protection.view().as_const() : image::PlaneView<const std::uint8_t>{},
         fixture.budget, scheduler});
}
inline methods::NlmFacts check_nlm_reference(NlmWorkingFixture& fixture, methods::NlmParameters p,
                                             unsigned workers) {
    const auto reference = nlm_working_reference(fixture, p);
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> mask_before(mask.begin(), mask.end());
    const auto held = fixture.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
    require(scheduler.workers() == workers, "requested real NLM worker count is retained");
    const auto result = run_nlm(fixture, p, scheduler);
    require(result && methods::validate(*result, p) && *result == reference.facts,
            "actual complete Working NLM Facts match independent direct patch/weight/mean/map "
            "definitions");
    const auto output = fixture.luminance.view().storage();
    for (std::size_t i = 0; i < output.size(); ++i) {
        const auto actual = output.subspan(i, 1).front();
        if (std::bit_cast<std::uint64_t>(reference.samples.at(i)) == before.at(i)) {
            require(std::bit_cast<std::uint64_t>(actual) == before.at(i),
                    "NLM protected, exact identity and row-padding samples keep entering bits");
        } else {
            require(std::isfinite(actual) && actual >= 0 && actual <= 1 &&
                        std::abs(actual - reference.samples.at(i)) < 1e-12,
                    "actual NLM output agrees with independently evaluated mean/correction at "
                    "existing1e-12 bound");
        }
    }
    require(std::ranges::equal(mask_before, fixture.protection.view().storage()) &&
                fixture.budget.used() == held,
            "all actual NLM owners join/refund and every protection backing byte remains exact");
    return *result;
}
inline void nlm_protect_fixture(NlmWorkingFixture& fixture) {
    for (std::uint32_t y = 0; y < 449; ++y) {
        for (std::uint32_t x = 0; x < 9; ++x) {
            if ((x + (3 * y)) % 11 == 0 || (y >= 64 && y < 128)) {
                fixture.protection.view().row(y).subspan(x, 1).front() = 255;
            }
        }
    }
}
inline void nlm_worker_cases() {
    std::optional<methods::NlmFacts> original;
    std::vector<std::uint64_t> original_bits;
    for (const auto workers : {1U, 2U, 7U}) {
        NlmWorkingFixture fixture{9, 449, nlm_working_test_scratch};
        nlm_protect_fixture(fixture);
        const auto facts = check_nlm_reference(fixture, {.h = 1, .patch = 3, .search = 7}, workers);
        const auto bits = gamma_bits(fixture.luminance.view().storage());
        require(
            facts.corrected_samples > 0 && facts.counts.changed > 0,
            "eight real64-row bands including a one-row tail attain NLM corrections and writes");
        if (original) {
            require(*original == facts && original_bits == bits,
                    "actual1/2/7 NLM schedulers produce identical full Y backing bits and Facts");
        } else {
            original = facts;
            original_bits = bits;
        }
    }
}
inline void nlm_absent_mask_case() {
    NlmWorkingFixture present{9, 11, nlm_working_test_scratch};
    NlmWorkingFixture absent{9, 11, nlm_working_test_scratch};
    const methods::NlmParameters p{.h = 1, .patch = 3, .search = 7};
    const auto expected = check_nlm_reference(present, p, 1);
    const auto held = absent.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = run_nlm(absent, p, scheduler, false);
    require(result && *result == expected && result->counts.changed > 0 &&
                gamma_bits(absent.luminance.view().storage()) ==
                    gamma_bits(present.luminance.view().storage()) &&
                absent.budget.used() == held,
            "fresh absent-mask and fresh all-zero-mask NLM paths agree on actual active "
            "output/Facts/refund");
}
inline void nlm_working_cases() {
    nlm_worker_cases();
    nlm_absent_mask_case();
}
} // namespace docenhance::tests
