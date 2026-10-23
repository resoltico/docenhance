// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/levels.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "levels_fixture.hpp"
#include "require.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace docenhance::tests {
inline constexpr methods::LevelsParameters default_levels_parameters{};
inline void levels_protect_fixture(LevelsFixture& fixture) {
    const auto width = fixture.luminance.width();
    const auto height = fixture.luminance.height();
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            if ((x + (3 * y)) % 7 == 0 || (y >= 10 && y < 16)) {
                fixture.protection.view().row(y).subspan(x, 1).front() = 255;
                fixture.luminance.view().row(y).subspan(x, 1).front() = -0.0;
            }
        }
    }
}
inline void levels_workers_case(std::uint32_t width, std::uint32_t height,
                                methods::LevelsParameters parameters) {
    std::vector<std::uint64_t> baseline;
    methods::LevelsFacts original;
    for (const unsigned workers : {1U, 2U, 7U}) {
        LevelsFixture fixture{width, height, levels_sample_bytes(std::size_t{width} * height)};
        levels_protect_fixture(fixture);
        const auto facts = check_levels_reference(fixture, parameters, workers);
        require(facts.counts.changed != 0, "the Levels reference exercises actual mapping writes");
        const auto bits = gamma_bits(fixture.luminance.view().storage());
        if (baseline.empty()) {
            baseline = bits;
            original = facts;
        } else {
            require(bits == baseline && facts == original,
                    "Levels buffers and facts are bit-identical for workers1/2/7");
        }
    }
}
inline void levels_active_cases() {
    levels_workers_case(31, 137, {.low = 10, .high = 90, .blend = 1});
    levels_workers_case(31, 137, {.low = 0.5, .high = 99.5, .blend = 0.6});
    levels_workers_case(1, 129, {.low = 0, .high = 100});
    levels_workers_case(2051, 1, {.low = 10, .high = 90});
}
inline void levels_flat_cases() {
    LevelsFixture narrow{8, 1, levels_sample_bytes(7)};
    for (std::uint32_t x = 0; x < 8; ++x) {
        narrow.luminance.view().row(0).subspan(x, 1).front() =
            0.25 + (static_cast<double>(x) * 1e-8);
    }
    narrow.protection.view().row(0).back() = 255;
    narrow.luminance.view().row(0).back() = 0.9;
    const auto flat = check_levels_reference(narrow, default_levels_parameters, 7);
    require(flat.reason == methods::LevelsReason::insufficient_dynamic_range &&
                flat.counts.evaluated == 0 && flat.counts.changed == 0 && flat.levels &&
                flat.levels->high > flat.levels->low,
            "a real nonconstant narrow fit retains its bounds without mapping");
    LevelsFixture constant{3, 2, levels_sample_bytes(6)};
    for (std::uint32_t y = 0; y < 2; ++y) {
        std::ranges::fill(constant.luminance.view().row(y), 0.4);
    }
    const auto result = check_levels_reference(constant, default_levels_parameters, 1);
    require(result.reason == methods::LevelsReason::insufficient_dynamic_range,
            "constant Levels data is a measured no-op");
    LevelsFixture singleton{3, 1, levels_sample_bytes(1)};
    std::ranges::fill(singleton.protection.view().row(0), std::uint8_t{255});
    singleton.protection.view().row(0).subspan(1, 1).front() = 0;
    const auto one = check_levels_reference(singleton, default_levels_parameters, 7);
    require(one.reason == methods::LevelsReason::insufficient_dynamic_range &&
                one.counts.eligible == 1,
            "a singleton fit succeeds with equal attained ranks");
}
inline void levels_noop_and_write_cases() {
    LevelsFixture no_workspace{3, 2, 0};
    const auto zero = check_levels_reference(no_workspace, {.blend = 0}, 7);
    require(zero.reason == methods::LevelsReason::zero_blend && !zero.levels,
            "zero blend requires no fitting workspace");
    for (std::uint32_t y = 0; y < 2; ++y) {
        std::ranges::fill(no_workspace.protection.view().row(y), std::uint8_t{255});
    }
    const auto protected_zero = check_levels_reference(no_workspace, {.blend = 0}, 7);
    const auto protected_only = check_levels_reference(no_workspace, default_levels_parameters, 7);
    require(protected_zero.reason == methods::LevelsReason::zero_blend &&
                protected_only.reason == methods::LevelsReason::no_eligible_samples,
            "zero blend precedes no eligibility before allocation");
    LevelsFixture endpoints{2, 1, levels_sample_bytes(2)};
    endpoints.luminance.view().row(0).front() = 0;
    endpoints.luminance.view().row(0).back() = 1;
    const auto written = check_levels_reference(endpoints, {.low = 0, .high = 100}, 7);
    require(endpoints.luminance.view().row(0).back() == 1 && written.counts.changed == 1 &&
                written.clipped_low_samples == 0 && written.clipped_high_samples == 0,
            "the attained white bound writes one over one without clipping tied endpoints");
    LevelsFixture tiny{21, 1, levels_sample_bytes(21)};
    const auto unchanged = check_levels_reference(
        tiny, {.low = 10, .high = 90, .blend = std::numeric_limits<double>::denorm_min()}, 1);
    require(unchanged.reason == methods::LevelsReason::no_effect &&
                unchanged.counts.evaluated == 21 && unchanged.clipped_low_samples != 0 &&
                unchanged.counts.changed == 0,
            "clipped tails need not produce stores under a rounded tiny blend");
}
inline void levels_resource_cases() {
    constexpr std::uint32_t width = 21;
    const auto bytes = levels_sample_bytes(width);
    require(bytes > std::size_t{width} * sizeof(double), "the exact fit charge includes alignment");
    LevelsFixture exact{width, 1, bytes};
    check_levels_reference(exact, default_levels_parameters, 1);
    LevelsFixture short_budget{width, 1, bytes - 1};
    const auto before = gamma_bits(short_budget.luminance.view().storage());
    const auto charge = short_budget.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result =
        methods::apply(methods::Levels::create().value(),
                       {short_budget.luminance.view(), short_budget.protection.view().as_const(),
                        short_budget.budget, scheduler});
    require(!result && result.error().code == core::ErrorCode::resource,
            "one real aligned fit byte short refuses before gathering or writes");
    require(gamma_bits(short_budget.luminance.view().storage()) == before &&
                short_budget.budget.used() == charge,
            "resource refusal preserves input and refunds the method charge");
}
inline void levels_cancellation_case(core::Checkpoint phase, std::size_t allowed) {
    LevelsFixture fixture{2051, 1, levels_sample_bytes(2051)};
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto charge = fixture.budget.used();
    const CheckpointStop stop{phase, allowed};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result =
        methods::apply(methods::Levels::create().value(),
                       {fixture.luminance.view(), fixture.protection.view().as_const(),
                        fixture.budget, scheduler});
    require(!result && result.error().code == core::ErrorCode::cancelled,
            "deterministic Levels phase stop returns no completed facts");
    require(CheckpointStop::visits() == allowed + 1,
            "the requested deterministic Levels phase boundary was reached");
    require(fixture.budget.used() == charge, "cancelled Levels releases its actual sample copy");
    if (phase != core::Checkpoint::processing || allowed == 0) {
        require(gamma_bits(fixture.luminance.view().storage()) == before,
                "pre-map cancellation leaves Y untouched");
    } else {
        require(gamma_bits(fixture.luminance.view().storage()) != before,
                "processing cancellation retains an actually modified private prefix");
    }
}
inline void levels_cancellation_cases() {
    levels_cancellation_case(core::Checkpoint::allocation, 0);
    levels_cancellation_case(core::Checkpoint::measurement, 1);
    levels_cancellation_case(core::Checkpoint::measurement, 5);
    levels_cancellation_case(core::Checkpoint::measurement, 7);
    levels_cancellation_case(core::Checkpoint::measurement, 10);
    levels_cancellation_case(core::Checkpoint::processing, 0);
    levels_cancellation_case(core::Checkpoint::processing, 1);
}
inline void levels_admission_cases() {
    LevelsFixture fixture{3, 2, 0};
    const CheckpointStop stop{core::Checkpoint::measurement, 0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(7, 7, 0, 0).value(),
                                    stop.cancellation()};
    const auto method = methods::Levels::create({.blend = 0}).value();
    const auto invalid = methods::apply(method, {{}, {}, fixture.budget, scheduler});
    require(!invalid && invalid.error().code == core::ErrorCode::argument,
            "malformed Levels views precede a no-op or observed stop");
    const auto result =
        methods::apply(method, {fixture.luminance.view(), fixture.protection.view().as_const(),
                                fixture.budget, scheduler});
    require(!result && result.error().code == core::ErrorCode::cancelled,
            "a valid Levels no-op still observes cancellation");
}
inline void levels_working_cases() {
    levels_active_cases();
    levels_flat_cases();
    levels_noop_and_write_cases();
    levels_resource_cases();
    levels_cancellation_cases();
    levels_admission_cases();
}
} // namespace docenhance::tests
