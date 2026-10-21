// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/morphology_working.hpp"
#include "docenhance/methods/parameters.hpp"
#include "gamma_fixture.hpp"
#include "morphology_working_fixture.hpp"
#include "morphology_working_reference.hpp"
#include "require.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace docenhance::tests {
inline bool morphology_analysis_close(const methods::MorphologyAnalysis& a,
                                      const methods::MorphologyAnalysis& b) {
    return a.count == b.count && a.stride == b.stride && a.fallback == b.fallback &&
           std::abs(a.analysis_fill - b.analysis_fill) < 1e-12 &&
           std::abs(a.target - b.target) < 1e-12 &&
           std::abs(a.background_q10 - b.background_q10) < 1e-12 &&
           std::abs(a.background_q50 - b.background_q50) < 1e-12 &&
           std::abs(a.background_q90 - b.background_q90) < 1e-12 &&
           std::abs(a.background_min - b.background_min) < 1e-12 &&
           std::abs(a.background_max - b.background_max) < 1e-12;
}
inline methods::MorphologyFacts check_morphology_reference(MorphologyWorkingFixture& fixture,
                                                           methods::MorphologyParameters p,
                                                           unsigned workers) {
    const auto expected = morphology_working_reference(fixture, p);
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> original_mask{mask.begin(), mask.end()};
    const auto resident = fixture.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
    const auto actual =
        methods::apply(methods::Morphology::create(p).value(),
                       {fixture.luminance.view(), fixture.protection.view().as_const(),
                        fixture.budget, scheduler});
    require(actual && methods::validate(*actual, p,
                                        {
                                            .width = fixture.luminance.width(),
                                            .height = fixture.luminance.height(),
                                        }),
            "actual Working morphology returns parameter/extent-admitted complete Facts");
    require(actual->counts == expected.facts.counts && actual->reason == expected.facts.reason &&
                actual->disposition == expected.facts.disposition &&
                actual->resolved_radius == expected.facts.resolved_radius &&
                actual->sigma == expected.facts.sigma &&
                actual->gaussian_radius == expected.facts.gaussian_radius &&
                actual->gain_capped_samples == expected.facts.gain_capped_samples &&
                actual->saturated_samples == expected.facts.saturated_samples &&
                actual->analysis.has_value() == expected.facts.analysis.has_value() &&
                std::abs(actual->min_gain - expected.facts.min_gain) < 1e-12 &&
                std::abs(actual->max_gain - expected.facts.max_gain) < 1e-12,
            "independent two-dimensional closing/Gaussian/linear-gain reference agrees on exact "
            "counts");
    if (actual->analysis && expected.facts.analysis) {
        require(morphology_analysis_close(*actual->analysis, *expected.facts.analysis),
                "actual analysis population/fill/ranks/full range/target match independent1e-12 "
                "reference");
    }
    const auto output = fixture.luminance.view().storage();
    for (std::size_t i = 0; i < expected.samples.size(); ++i) {
        const auto value = output.subspan(i, 1).front();
        if (std::bit_cast<std::uint64_t>(expected.samples.at(i)) == before.at(i)) {
            require(std::bit_cast<std::uint64_t>(value) == before.at(i),
                    "protected, identity and row padding retain their full entering bits");
        } else {
            require(std::isfinite(value) && value >= 0 && value <= 1 &&
                        std::abs(value - expected.samples.at(i)) < 1e-12,
                    "differing linear Y agrees with the independent reference");
        }
    }
    require(fixture.budget.used() == resident && std::ranges::equal(mask, original_mask),
            "actual phase owners refund after joined return and mask bytes stay unchanged");
    return *actual;
}
inline void morphology_protect_fixture(MorphologyWorkingFixture& fixture) {
    for (std::uint32_t y = 0; y < 137; ++y) {
        for (std::uint32_t x = 0; x < 33; ++x) {
            if ((x + (3 * y)) % 7 == 0 || (y >= 10 && y < 16)) {
                fixture.protection.view().row(y).subspan(x, 1).front() = 255;
                fixture.luminance.view().row(y).subspan(x, 1).front() = -0.0;
            }
        }
    }
}
inline void morphology_worker_case() {
    std::vector<std::uint64_t> original;
    std::optional<methods::MorphologyFacts> facts;
    for (const auto workers : {1U, 2U, 7U}) {
        MorphologyWorkingFixture fixture{33, 137, morphology_test_scratch};
        morphology_protect_fixture(fixture);
        const auto actual =
            check_morphology_reference(fixture, {.target = 0.9, .radius = 2}, workers);
        const auto bits = gamma_bits(fixture.luminance.view().storage());
        require(actual.counts.changed != 0, "worker/seam comparison attains genuine Y writes");
        if (!facts) {
            facts = actual;
            original = bits;
        } else {
            require(actual == *facts && bits == original,
                    "1/2/7 real workers produce identical whole Y storage and complete Facts bits");
        }
    }
}
inline void morphology_constant_and_excursion_cases() {
    for (const auto value : {0.0, 0.25, 1.0}) {
        MorphologyWorkingFixture fixture{5, 5, morphology_test_scratch};
        for (std::uint32_t y = 0; y < 5; ++y) {
            std::ranges::fill(fixture.luminance.view().row(y), value);
        }
        check_morphology_reference(fixture, {.radius = 1}, 7);
        const auto facts = check_morphology_reference(fixture, {.target = 1, .radius = 1}, 7);
        require(facts.counts.evaluated == 25 && (value != 0 || facts.gain_capped_samples == 25),
                "active black can cap every gain while retaining exact black and no writes");
    }
    MorphologyWorkingFixture fixture{5, 5, morphology_test_scratch};
    for (std::uint32_t y = 0; y < 5; ++y) {
        std::ranges::fill(fixture.luminance.view().row(y), 0.25);
    }
    fixture.luminance.view().row(2).subspan(2, 1).front() = 1;
    const auto tiny =
        check_morphology_reference(fixture, {.strength = 1e-300, .target = 1, .radius = 1}, 7);
    require(tiny.counts.changed == 0 && tiny.max_gain > 1,
            "tiny positive strength rounds to a real linear no-store without erasing gain "
            "observations");
    const auto saturated = check_morphology_reference(fixture, {.target = 1, .radius = 1}, 7);
    require(saturated.saturated_samples > 0 &&
                fixture.luminance.view().row(2).subspan(2, 1).front() == 1,
            "white can saturate its raw target while its exact entering endpoint is not stored");
}
inline void morphology_fallback_case() {
    MorphologyWorkingFixture fixture{1025, 1, morphology_test_scratch};
    std::ranges::fill(fixture.protection.view().row(0), 255);
    for (std::uint32_t i = 0; i < 17; ++i) {
        fixture.protection.view().row(0).subspan((2 * i) + 1, 1).front() = 0;
    }
    fixture.luminance.view().row(0).subspan(1, 1).front() = 0.2;
    const auto facts = check_morphology_reference(fixture, {.radius = 1}, 7);
    require(facts.analysis && facts.analysis->fallback && facts.analysis->stride == 2 &&
                facts.analysis->count == 1 && facts.analysis->analysis_fill == 0.2 &&
                facts.analysis->background_min < facts.analysis->background_max,
            "first-only fallback does not collapse the whole protected-fill background range");
}
inline void morphology_gain_boundary_cases() {
    MorphologyWorkingFixture floor{5, 5, morphology_test_scratch};
    for (std::uint32_t y = 0; y < 5; ++y) {
        std::ranges::fill(floor.luminance.view().row(y), 0.001);
    }
    const auto dark =
        check_morphology_reference(floor, {.max_gain = 4, .target = 0.1, .radius = 1}, 7);
    require(dark.gain_capped_samples == 25 && dark.counts.changed == 25 && dark.min_gain == 4,
            "actual dark field uses the exact0.02 gain floor before clamping");
    MorphologyWorkingFixture cap{5, 5, morphology_test_scratch};
    for (std::uint32_t y = 0; y < 5; ++y) {
        std::ranges::fill(cap.luminance.view().row(y), 0.25);
    }
    const auto equal_cap = check_morphology_reference(cap, {.target = 0.5, .radius = 1}, 7);
    require(equal_cap.gain_capped_samples == 25,
            "unclamped ratio exactly equal to cap remains counted inclusively");
    for (std::uint32_t y = 0; y < 5; ++y) {
        std::ranges::fill(cap.luminance.view().row(y), 0.25);
    }
    const auto endpoint =
        check_morphology_reference(cap, {.max_gain = 4, .target = 1, .radius = 1}, 7);
    require(endpoint.saturated_samples == 0 && endpoint.counts.changed == 25 &&
                cap.luminance.view().row(0).front() == 1,
            "raw target exactly one changes Y without a false strict-saturation observation");
}
inline void morphology_absent_mask_case() {
    MorphologyWorkingFixture present{17, 19, morphology_test_scratch};
    MorphologyWorkingFixture absent{17, 19, morphology_test_scratch};
    const methods::MorphologyParameters p{.target = 0.9, .radius = 2};
    const auto expected = check_morphology_reference(present, p, 1);
    const auto resident = absent.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto actual = methods::apply(methods::Morphology::create(p).value(),
                                       {absent.luminance.view(), {}, absent.budget, scheduler});
    require(actual && actual->counts.changed > 0 && *actual == expected &&
                gamma_bits(absent.luminance.view().storage()) ==
                    gamma_bits(present.luminance.view().storage()) &&
                absent.budget.used() == resident,
            "fresh active absent mask executes actual sample/fill/map and matches all-zero-mask "
            "output bits/Facts/refund");
}
} // namespace docenhance::tests
