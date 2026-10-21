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
#include "docenhance/methods/gamma.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace docenhance::tests {
inline void gamma_workers_case(methods::GammaParameters parameters, std::uint32_t width,
                               std::uint32_t height) {
    std::vector<std::uint64_t> baseline;
    methods::GammaFacts first;
    for (const unsigned workers : {1U, 2U, 7U}) {
        GammaFixture fixture{width, height};
        const auto facts = check_gamma_reference(fixture, parameters, workers);
        require(facts.counts.changed != 0, "the reference fixture exercises actual Gamma writes");
        const auto bits = gamma_bits(fixture.luminance.view().storage());
        if (baseline.empty()) {
            baseline = bits;
            first = facts;
        } else {
            require(bits == baseline && facts == first,
                    "fixed Gamma partitions give identical buffers and facts for workers1/2/7");
        }
    }
}
inline void gamma_active_cases() {
    const std::array parameters{
        methods::GammaParameters{.gamma = 2, .blend = 1},
        methods::GammaParameters{.gamma = 0.25, .blend = 1},
        methods::GammaParameters{.gamma = 0.6, .blend = 0.35},
        methods::GammaParameters{.gamma = 1.2, .blend = 0.6},
    };
    for (const auto method : parameters) {
        gamma_workers_case(method, 31, 137);
    }
    gamma_workers_case({.gamma = 2}, 1, 129);
    gamma_workers_case({.gamma = 0.25}, 2051, 1);
}
inline void gamma_identity_cases() {
    GammaFixture fixture{3, 2};
    const auto zero = check_gamma_reference(fixture, {.gamma = 2, .blend = 0}, 7);
    require(zero.reason == methods::GammaReason::zero_blend && zero.counts.evaluated == 0,
            "zero blend admits and counts the actual cohort without numerical work");
    const auto identity = check_gamma_reference(fixture, {.gamma = 1}, 7);
    require(identity.reason == methods::GammaReason::identity_gamma,
            "global gamma one preserves entering bits");
    for (std::uint32_t y = 0; y < fixture.protection.view().height(); ++y) {
        std::ranges::fill(fixture.protection.view().row(y), 255);
    }
    const auto protected_zero = check_gamma_reference(fixture, {.gamma = 1, .blend = 0}, 7);
    require(protected_zero.reason == methods::GammaReason::zero_blend,
            "zero blend precedes no eligibility");
    const auto protected_identity = check_gamma_reference(fixture, {.gamma = 1}, 7);
    require(protected_identity.reason == methods::GammaReason::no_eligible_samples,
            "no eligibility precedes gamma one");
}
inline void gamma_write_event_cases() {
    GammaFixture white{1, 1};
    white.protection.view().row(0).front() = 0;
    white.luminance.view().row(0).front() = 1;
    const auto written = check_gamma_reference(white, {.gamma = 0.25}, 7);
    require(white.luminance.view().row(0).front() == 1 && written.counts.changed == 1 &&
                written.disposition == methods::Disposition::applied,
            "a perceptual change counts the real write of one over one");
    GammaFixture black{2, 1};
    std::ranges::fill(black.protection.view().row(0), 0);
    std::ranges::fill(black.luminance.view().row(0), -0.0);
    const auto unchanged = check_gamma_reference(black, {.gamma = 2}, 1);
    require(unchanged.reason == methods::GammaReason::no_effect && unchanged.counts.evaluated == 2,
            "active candidate equality still evaluates every eligible sample without stores");
    GammaFixture tiny{2, 1};
    std::ranges::fill(tiny.protection.view().row(0), 0);
    std::ranges::fill(tiny.luminance.view().row(0), 0.25);
    const auto rounded = check_gamma_reference(
        tiny, {.gamma = 2, .blend = std::numeric_limits<double>::denorm_min()}, 7);
    require(rounded.reason == methods::GammaReason::no_effect && rounded.counts.evaluated == 2,
            "rounded blend equality preserves exact entering samples without decoding");
}
inline void gamma_cancellation_cases() {
    GammaFixture fixture{2051, 1};
    std::ranges::fill(fixture.protection.view().row(0), 0);
    std::ranges::fill(fixture.luminance.view().row(0), 0.25);
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto method = methods::Gamma::create({.gamma = 2}).value();
    const CheckpointStop initial{core::Checkpoint::processing, 0};
    const exec::Scheduler stopped{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                  initial.cancellation()};
    const auto first =
        methods::apply(method, {fixture.luminance.view(), {}, fixture.budget, stopped});
    require(!first && first.error().code == core::ErrorCode::cancelled,
            "a processing stop before the first pixel returns no completed Gamma facts");
    require(gamma_bits(fixture.luminance.view().storage()) == before,
            "a pre-write Gamma stop preserves all storage");
    const CheckpointStop interior{core::Checkpoint::processing, 1};
    const exec::Scheduler interrupted{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                      interior.cancellation()};
    const auto result =
        methods::apply(method, {fixture.luminance.view(), {}, fixture.budget, interrupted});
    require(!result && result.error().code == core::ErrorCode::cancelled,
            "an interior stop returns an error rather than partial success");
    require(CheckpointStop::visits() == 2,
            "a wide singleton row reaches the existing bounded interior");
    const auto row = fixture.luminance.view().row(0);
    require(std::ranges::all_of(row.first(1024), [](double value) { return value != 0.25; }),
            "work before the deterministic stop actually writes its prefix");
    require(std::ranges::all_of(row.subspan(1024), [](double value) { return value == 0.25; }),
            "unstarted Gamma suffix remains untouched");
    require(fixture.budget.available() == 0, "cancellation introduces no charged method workspace");
}
inline void gamma_admission_cases() {
    GammaFixture fixture{3, 2};
    const CheckpointStop stop{core::Checkpoint::measurement, 0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(7, 7, 0, 0).value(),
                                    stop.cancellation()};
    const auto method = methods::Gamma::create({.gamma = 1, .blend = 0}).value();
    const auto empty = methods::apply(method, {{}, {}, fixture.budget, scheduler});
    require(!empty && empty.error().code == core::ErrorCode::argument,
            "invalid Gamma geometry is refused before a no-op or stop");
    const auto result =
        methods::apply(method, {fixture.luminance.view(), fixture.protection.view().as_const(),
                                fixture.budget, scheduler});
    require(!result && result.error().code == core::ErrorCode::cancelled,
            "a valid algebraic no-op cannot override observed measurement cancellation");
}
inline void gamma_working_cases() {
    gamma_active_cases();
    gamma_identity_cases();
    gamma_write_event_cases();
    gamma_cancellation_cases();
    gamma_admission_cases();
}
} // namespace docenhance::tests
