// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/methods/tvl1_working.hpp"
#include "docenhance/methods/warnings.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "sharpen_reference.hpp"
#include "tvl1_working_fixture.hpp"
#include "tvl1_working_reference.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace docenhance::tests {
inline constexpr methods::Tvl1Parameters active_tvl1_parameters{
    .lambda = 0.05,
    .iterations = 30,
    .tolerance = 1e-8,
    .blend = 1,
};
inline methods::Tvl1Facts check_tvl1_reference(Tvl1WorkingFixture& fixture,
                                               methods::Tvl1Parameters parameters,
                                               unsigned workers) {
    const auto expected = tvl1_working_reference(fixture, parameters);
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> original_mask{mask.begin(), mask.end()};
    const auto charge = fixture.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
    std::optional<methods::Tvl1Stop> observed_stop;
    const auto result = methods::apply(
        methods::Tvl1::create(parameters).value(),
        {fixture.luminance.view(), fixture.protection.view().as_const(), fixture.budget, scheduler},
        observed_stop);
    require(result.has_value(), "actual Working TV-L1 succeeds");
    require(methods::validate(*result, parameters) &&
                result->disposition == expected.facts.disposition &&
                result->reason == expected.facts.reason &&
                result->counts == expected.facts.counts &&
                result->context_samples == expected.facts.context_samples &&
                result->corrected_samples == expected.facts.corrected_samples &&
                result->solver.has_value() == expected.facts.solver.has_value(),
            "TV-L1 exact tags and counters match independent full-field solve and D3 mapping");
    if (result->solver && expected.facts.solver) {
        require(tvl1_solution_matches(*result->solver, *expected.facts.solver),
                "solver stop metrics and both energies retain independent1e-12/1e-10 bounds");
    }
    require(observed_stop == (result->solver ? std::optional{result->solver->stop} : std::nullopt),
            "caller retains the actual stop until completed Facts admission");
    const bool limit = result->solver && result->solver->stop == methods::Tvl1Stop::iteration_limit;
    require(methods::warnings(*result).front().has_value() == limit,
            "iteration warning derives from real stop rather than requested cap or presence");
    const auto output = fixture.luminance.view().storage();
    for (std::size_t i = 0; i < expected.samples.size(); ++i) {
        const auto actual = output.subspan(i, 1).front();
        if (std::bit_cast<std::uint64_t>(expected.samples.at(i)) == before.at(i)) {
            require(std::bit_cast<std::uint64_t>(actual) == before.at(i),
                    "TV-L1 exact protected padding and identity bits remain unchanged");
        } else {
            require(std::isfinite(actual) && actual >= 0 && actual <= 1 &&
                        std::abs(actual - expected.samples.at(i)) < 1e-12,
                    "TV-L1 output matches independent simultaneous scattered-adjoint reference");
        }
    }
    require(std::ranges::equal(mask, original_mask) && fixture.budget.used() == charge,
            "TV-L1 leaves mask unchanged and refunds all five fields after joined return");
    return *result;
}
inline void tvl1_protect_fixture(Tvl1WorkingFixture& fixture) {
    for (std::uint32_t y = 0; y < fixture.luminance.height(); ++y) {
        for (std::uint32_t x = 0; x < fixture.luminance.width(); ++x) {
            if ((x + (3 * y)) % 7 == 0 || (y >= 10 && y < 16)) {
                fixture.protection.view().row(y).subspan(x, 1).front() = 255;
                fixture.luminance.view().row(y).subspan(x, 1).front() = -0.0;
            }
        }
    }
}
inline void tvl1_workers_case(std::uint32_t width, std::uint32_t height,
                              methods::Tvl1Parameters parameters) {
    std::vector<std::uint64_t> baseline;
    methods::Tvl1Facts original;
    for (const unsigned workers : {1U, 2U, 7U}) {
        Tvl1WorkingFixture fixture{width, height, 5 * tvl1_plane_bytes(width, height)};
        tvl1_protect_fixture(fixture);
        const auto facts = check_tvl1_reference(fixture, parameters, workers);
        require(facts.counts.changed != 0,
                "TV-L1 worker reference exercises actual smoothing writes");
        const auto bits = gamma_bits(fixture.luminance.view().storage());
        if (baseline.empty()) {
            baseline = bits;
            original = facts;
        } else {
            require(
                bits == baseline && facts == original,
                "TV-L1 Y backing and complete solution facts are bit-identical for1/2/7 workers");
        }
    }
}
inline void tvl1_active_cases() {
    tvl1_workers_case(33, 137, active_tvl1_parameters);
    tvl1_workers_case(17, 19, {.lambda = 0.1, .iterations = 20, .tolerance = 1e-8, .blend = 0.6});
    tvl1_workers_case(1, 129, active_tvl1_parameters);
    tvl1_workers_case(129, 1, active_tvl1_parameters);
}
inline void tvl1_precision_case() {
    const auto low = sharpen_reference_srgb_decode(30000.0 / 65535);
    const auto high = sharpen_reference_srgb_decode(30001.0 / 65535);
    require(std::isfinite(low) && std::isfinite(high) && low >= 0 && high <= 1 && low < high,
            "independent IEC decoding preserves adjacent16bit source levels");
    const methods::Tvl1Parameters parameters{
        .lambda = 20,
        .iterations = 30,
        .tolerance = 1e-8,
        .blend = 1,
    };
    std::vector<std::uint64_t> baseline;
    methods::Tvl1Facts original;
    for (const unsigned workers : {1U, 2U, 7U}) {
        Tvl1WorkingFixture fixture{17, 19, 5 * tvl1_plane_bytes(17, 19)};
        for (std::uint32_t y = 0; y < 19; ++y) {
            for (std::uint32_t x = 0; x < 17; ++x) {
                fixture.luminance.view().row(y).subspan(x, 1).front() =
                    (x + y) % 2 == 0 ? low : high;
            }
        }
        const auto facts = check_tvl1_reference(fixture, parameters, workers);
        require(facts.solver && facts.context_samples == 323 && facts.counts.evaluated == 323 &&
                    fixture.luminance.view().row(0).front() == low &&
                    fixture.luminance.view().row(0).subspan(1, 1).front() == high,
                "real Working16bit-derived levels stay distinct with actual full context and "
                "evaluation");
        if (facts.solver) {
            require(std::isfinite(facts.solver->objective_start) &&
                        facts.solver->objective_start > 0 &&
                        facts.solver->objective_end == facts.solver->objective_start,
                    "actual positive ordered energy detects silent8bit solver-input collapse");
        }
        const auto bits = gamma_bits(fixture.luminance.view().storage());
        if (baseline.empty()) {
            baseline = bits;
            original = facts;
        } else {
            require(
                bits == baseline && facts == original,
                "one-code16bit Working output backing and Facts match exactly for1/2/7 workers");
        }
    }
}
inline void tvl1_constant_cases() {
    for (const auto cap : {10U, 20U, 29U, 30U, 150U}) {
        Tvl1WorkingFixture fixture{5, 3, 5 * tvl1_plane_bytes(5, 3)};
        for (std::uint32_t y = 0; y < 3; ++y) {
            std::ranges::fill(fixture.luminance.view().row(y), 0.4);
        }
        const auto facts = check_tvl1_reference(fixture, {.iterations = cap}, 7);
        require(facts.solver && facts.counts.changed == 0 && facts.corrected_samples == 0 &&
                    facts.context_samples == 15 && facts.counts.evaluated == 15,
                "active constant solves keep full context and actual evaluations");
        if (facts.solver) {
            require(facts.solver->iterations == std::min(cap, 30U) &&
                        facts.solver->stop == (cap < 30 ? methods::Tvl1Stop::iteration_limit
                                                        : methods::Tvl1Stop::tolerance_met),
                    "short and noncheckpoint caps retain streak while tolerance wins at cap30");
        }
    }
    Tvl1WorkingFixture singleton{1, 1, 5 * tvl1_plane_bytes(1, 1)};
    check_tvl1_reference(singleton, {.iterations = 30}, 7);
}
inline void tvl1_high_lambda_case() {
    Tvl1WorkingFixture fixture{7, 1, 5 * tvl1_plane_bytes(7, 1)};
    for (std::uint32_t x = 0; x < 7; ++x) {
        fixture.luminance.view().row(0).subspan(x, 1).front() =
            sharpen_reference_srgb_decode(0.4 + (static_cast<double>(x) * 1e-4));
    }
    const auto facts = check_tvl1_reference(
        fixture, {.lambda = 20, .iterations = 30, .tolerance = 1e-8, .blend = 1}, 7);
    require(facts.solver && facts.corrected_samples == 0 && facts.counts.changed == 0,
            "nonconstant high fidelity can retain every primal sample");
    if (facts.solver) {
        require(facts.solver->primal_update == 0 && facts.solver->dual_update > 1e-8 &&
                    facts.solver->objective_start > 0 &&
                    facts.solver->objective_end == facts.solver->objective_start &&
                    facts.solver->stop == methods::Tvl1Stop::iteration_limit,
                "one scheduled objective owner keeps exact energy while dual motion prevents false "
                "stop");
    }
}
inline void tvl1_equal_decoded_write_case() {
    Tvl1WorkingFixture fixture{2, 1, 5 * tvl1_plane_bytes(2, 1)};
    fixture.luminance.view().row(0).front() = 0x1.0624dd2f1a9edp-10;
    fixture.luminance.view().row(0).back() = 0x1.80b1bd0e6ffc3p-9;
    const auto first = fixture.luminance.view().row(0).front();
    const methods::Tvl1Parameters parameters{
        .lambda = 0x1.ffffffffffffep-5,
        .iterations = 10,
        .tolerance = 1e-8,
        .blend = 1,
    };
    const auto expected = tvl1_working_reference(fixture, parameters);
    require(expected.facts.corrected_samples == 2 && expected.facts.counts.changed == 2 &&
                expected.samples.front() == first,
            "valid linear-IEC reference fixture has two actual perceptual corrections but one "
            "decoded-equal target");
    const auto facts = check_tvl1_reference(fixture, parameters, 7);
    require(facts.corrected_samples == 2 && facts.counts.changed == 2 &&
                fixture.luminance.view().row(0).front() == first,
            "full-blend TV-L1 counts the logical target write even when decoded Y stays identical");
}
inline void tvl1_context_and_rounding_cases() {
    Tvl1WorkingFixture low{7, 3, 5 * tvl1_plane_bytes(7, 3)};
    Tvl1WorkingFixture high{7, 3, 5 * tvl1_plane_bytes(7, 3)};
    for (std::uint32_t y = 0; y < 3; ++y) {
        std::ranges::fill(low.luminance.view().row(y), 0.4);
        std::ranges::fill(high.luminance.view().row(y), 0.4);
    }
    low.protection.view().row(1).subspan(3, 1).front() = 255;
    high.protection.view().row(1).subspan(3, 1).front() = 255;
    low.luminance.view().row(1).subspan(3, 1).front() = -0.0;
    high.luminance.view().row(1).subspan(3, 1).front() = 1;
    const methods::Tvl1Parameters context_parameters{
        .lambda = 0.05,
        .iterations = 10,
        .tolerance = 1e-8,
        .blend = 1,
    };
    const auto a = check_tvl1_reference(low, context_parameters, 1);
    const auto b = check_tvl1_reference(high, context_parameters, 7);
    require(a.context_samples == 21 && b.context_samples == 21 && a.counts.eligible == 20 &&
                low.luminance.view().row(1).subspan(2, 1).front() !=
                    high.luminance.view().row(1).subspan(2, 1).front(),
            "protected values remain solver context and influence eligible neighbors");
    Tvl1WorkingFixture tiny{21, 3, 5 * tvl1_plane_bytes(21, 3)};
    const auto rounded = check_tvl1_reference(
        tiny,
        {.lambda = 0.05, .iterations = 10, .blend = std::numeric_limits<double>::denorm_min()}, 1);
    require(rounded.corrected_samples != 0 && rounded.counts.changed == 0 &&
                rounded.context_samples == 63,
            "actual solver corrections can round to no output stores under an admitted tiny blend");
}
} // namespace docenhance::tests
