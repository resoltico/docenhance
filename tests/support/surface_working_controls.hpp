// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "surface_working_cases.hpp"
#include "surface_working_fixture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
namespace docenhance::tests {
inline void surface_noop_cases() {
    SurfaceWorkingFixture fixture{8, 8, 0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> mask_before(mask.begin(), mask.end());
    const auto zero = run_surface(fixture, {.strength = 0, .max_gain = 1}, scheduler);
    require(gamma_bits(fixture.luminance.view().storage()) == before &&
                std::ranges::equal(mask_before, fixture.protection.view().storage()),
            "zero strength preserves whole Y/padding bits and full mask bytes immediately");
    const auto unit = run_surface(fixture, {.max_gain = 1}, scheduler);
    require(gamma_bits(fixture.luminance.view().storage()) == before &&
                std::ranges::equal(mask_before, fixture.protection.view().storage()),
            "unit gain preserves whole Y/padding bits and full mask bytes immediately");
    require(zero && zero->reason == methods::SurfaceReason::zero_strength && unit &&
                unit->reason == methods::SurfaceReason::unit_gain,
            "I01 zero strength precedes unit gain without scratch");
    std::ranges::fill(fixture.protection.view().storage(), std::uint8_t{255});
    const std::vector<std::uint8_t> protected_before(mask.begin(), mask.end());
    const auto empty = run_surface(fixture, {.strength = 0, .max_gain = 1}, scheduler);
    require(gamma_bits(fixture.luminance.view().storage()) == before &&
                std::ranges::equal(protected_before, fixture.protection.view().storage()),
            "all-protected E0 preserves whole Y/padding bits and the intentionally filled mask");
    require(empty && empty->reason == methods::SurfaceReason::no_eligible_samples &&
                fixture.budget.peak() == fixture.budget.used(),
            "I01 no eligible samples precedes both algebraic no-ops");
    const CheckpointStop stop{core::Checkpoint::measurement, 0};
    const exec::Scheduler stopped{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                  stop.cancellation()};
    const auto invalid = methods::apply(methods::Surface::create({.strength = 0}).value(),
                                        {{}, {}, fixture.budget, stopped});
    require(!invalid && invalid.error().code == core::ErrorCode::argument &&
                CheckpointStop::visits() == 0,
            "malformed Working outranks no-op and cancellation");
    const auto cancelled = run_surface(fixture, {.strength = 0}, stopped);
    require(!cancelled && cancelled.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == 1,
            "valid no-op observes eligibility cancellation");
}
inline void surface_automatic_cases() {
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    SurfaceWorkingFixture sparse{17, 1, surface_test_scratch};
    sparse.protection.view().row(0).front() = 255;
    sparse.protection.view().row(0).back() = 255;
    const methods::SurfaceParameters defaults;
    const auto refused = run_surface(sparse, defaults, scheduler);
    const auto skipped = run_surface(sparse, {.mode = methods::SurfaceMode::automatic}, scheduler);
    require(!refused && refused.error().code == core::ErrorCode::method_inapplicable && skipped &&
                skipped->reason == methods::SurfaceReason::insufficient_samples && !skipped->solver,
            "E15 real census distinguishes explicit refusal from early automatic skip");
    SurfaceWorkingFixture dark{8, 8, surface_test_scratch};
    std::ranges::fill(dark.luminance.view().row(0), 0.001);
    for (unsigned y = 1; y < 8; ++y) {
        std::ranges::fill(dark.luminance.view().row(y), 0.001);
    }
    const auto unmeasured = run_surface(dark, {.cell = 8}, scheduler);
    const auto unfit =
        run_surface(dark, {.mode = methods::SurfaceMode::automatic, .cell = 8}, scheduler);
    require(!unmeasured && unmeasured.error().code == core::ErrorCode::method_inapplicable &&
                unfit && unfit->reason == methods::SurfaceReason::insufficient_cells &&
                !unfit->solver && !unfit->measurements,
            "dark cells cannot fabricate a fit or completed application");
    SurfaceWorkingFixture constant{8, 8, surface_test_scratch};
    for (unsigned y = 0; y < 8; ++y) {
        std::ranges::fill(constant.luminance.view().row(y), 0.5);
    }
    const auto before = gamma_bits(constant.luminance.view().storage());
    const auto fitted =
        run_surface(constant, {.mode = methods::SurfaceMode::automatic, .cell = 8}, scheduler);
    require(fitted && fitted->reason == methods::SurfaceReason::automatic_predicates &&
                fitted->solver && fitted->measurements && fitted->counts.evaluated == 0 &&
                fitted->auto_predicates.background_variation == false &&
                gamma_bits(constant.luminance.view().storage()) == before,
            "real fitted automatic skip retains immutable analysis without stores");
}
inline auto surface_encoded_triplet(double y) {
    const image::RasterShape shape{
        .width = 1,
        .height = 1,
        .model = image::SampleModel::rgb,
        .depth = image::SampleDepth::word(),
    };
    const std::array samples{y, y, y};
    std::array<std::uint8_t, 6> bytes{};
    require(image::quantize_linear(shape, samples, bytes).has_value(),
            "actual 16-bit quantizer admits the independent equal-channel representation probe");
    return bytes;
}
inline void surface_gain_cases() {
    for (const auto strength : {1.0, 1e-16, 1e-300}) {
        SurfaceWorkingFixture fixture{8, 8, surface_test_scratch};
        for (unsigned y = 0; y < 8; ++y) {
            std::ranges::fill(fixture.luminance.view().row(y), 0.25);
        }
        fixture.luminance.view().row(0).front() = 1;
        const auto facts = check_surface_reference(
            fixture, {.strength = strength, .max_gain = 4, .target = 1, .cell = 8}, 1);
        require(facts.gain_capped_samples == 64 && facts.min_gain == 4 && facts.max_gain == 4 &&
                    facts.saturated_samples == (strength == 1e-300 ? 0U : 1U) &&
                    facts.counts.changed == (strength == 1e-300 ? 0U : 63U),
                "inclusive cap and strict raw saturation precede endpoint/tiny-strength identity");
        if (strength == 1e-16) {
            const auto quarter = fixture.luminance.view().row(0).subspan(1, 1).front();
            require(quarter > 0.25 && facts.counts.changed == 63 &&
                        surface_encoded_triplet(quarter) == surface_encoded_triplet(0.25),
                    "actual differing linear Y is stored/counts even when byte quantization is "
                    "identical");
        }
    }
    SurfaceWorkingFixture zero{8, 8, surface_test_scratch};
    zero.luminance.view().row(0).front() = -0.0;
    const auto facts = check_surface_reference(zero, {.target = 0.8, .cell = 8}, 1);
    require(facts.counts.changed > 0 && std::signbit(zero.luminance.view().row(0).front()),
            "active fit preserves unprotected signed-zero numeric identity without a store");
}
inline void surface_resource_cases() {
    // C1: L64,W448,Q512; constant residual avoids Hmg. L+2S = 1088 dominates.
    for (const auto scratch : {std::size_t{1088}, std::size_t{1087}}) {
        SurfaceWorkingFixture fixture{8, 8, scratch};
        for (unsigned y = 0; y < 8; ++y) {
            std::ranges::fill(fixture.luminance.view().row(y), 0.25);
        }
        const auto held = fixture.budget.used();
        const auto before = gamma_bits(fixture.luminance.view().storage());
        const CheckpointStop observe{core::Checkpoint::allocation,
                                     std::numeric_limits<std::size_t>::max()};
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                        observe.cancellation()};
        const auto result = run_surface(fixture, {.target = 1, .cell = 8}, scheduler);
        require(CheckpointStop::visits() == 4 && fixture.budget.used() == held,
                "four actual work/log/bank/statistics allocation checkpoints refund");
        if (scratch == 1088) {
            require(result && result->counts.changed == 64 &&
                        fixture.budget.peak() == held + scratch,
                    "exact aligned phase peak succeeds through actual map");
        } else {
            require(!result && result.error().code == core::ErrorCode::resource &&
                        fixture.budget.peak() == held + 1024 &&
                        gamma_bits(fixture.luminance.view().storage()) == before,
                    "one-byte-short actual paired-statistics acquisition refuses before map");
        }
    }
}
inline void surface_cancel_cases() {
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::measurement,
             core::Checkpoint::solving,
             core::Checkpoint::processing,
             core::Checkpoint::scheduling,
         }) {
        std::size_t visits = 0;
        {
            SurfaceWorkingFixture fixture{17, 19, surface_test_scratch};
            const CheckpointStop observe{phase, std::numeric_limits<std::size_t>::max()};
            const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                            observe.cancellation()};
            const auto result = run_surface(fixture, {.target = 0.8, .cell = 8}, scheduler);
            require(result.has_value(), "healthy actual phase counterpart calibrates visits");
            visits = CheckpointStop::visits();
        }
        require(visits > 0, "every required phase actually executes");
        for (std::size_t allowed = 0; allowed < visits; ++allowed) {
            SurfaceWorkingFixture fixture{17, 19, surface_test_scratch};
            const auto held = fixture.budget.used();
            const auto before = gamma_bits(fixture.luminance.view().storage());
            const CheckpointStop stop{phase, allowed};
            const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                            stop.cancellation()};
            const auto result = run_surface(fixture, {.target = 0.8, .cell = 8}, scheduler);
            require(!result && result.error().code == core::ErrorCode::cancelled &&
                        CheckpointStop::visits() == allowed + 1 && fixture.budget.used() == held,
                    "actual bounded phase stop returns no Facts and refunds joined owners");
            if (phase == core::Checkpoint::allocation || phase == core::Checkpoint::measurement ||
                phase == core::Checkpoint::solving) {
                require(gamma_bits(fixture.luminance.view().storage()) == before,
                        "premap phase cancellation leaves complete Y backing unchanged");
            }
        }
    }
}
} // namespace docenhance::tests
