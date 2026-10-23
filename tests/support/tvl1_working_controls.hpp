// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/tvl1_working.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "tvl1_parallel_cases.hpp"
#include "tvl1_stop_cases.hpp"
#include "tvl1_working_cases.hpp"
#include "tvl1_working_fixture.hpp"
#include "tvl1_working_reference.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace docenhance::tests {
inline auto run_tvl1(Tvl1WorkingFixture& fixture, methods::Tvl1Parameters parameters,
                     const exec::Scheduler& scheduler) {
    std::optional<methods::Tvl1Stop> observed_stop;
    return methods::apply(
        methods::Tvl1::create(parameters).value(),
        {fixture.luminance.view(), fixture.protection.view().as_const(), fixture.budget, scheduler},
        observed_stop);
}
inline void tvl1_noop_and_admission_cases() {
    Tvl1WorkingFixture fixture{3, 2, 0};
    const auto zero = check_tvl1_reference(fixture, {.blend = 0}, 7);
    require(zero.reason == methods::Tvl1Reason::zero_blend && !zero.solver &&
                zero.context_samples == 0,
            "zero blend consumes no five-plane workspace or solver metadata");
    for (std::uint32_t y = 0; y < 2; ++y) {
        std::ranges::fill(fixture.protection.view().row(y), std::uint8_t{255});
    }
    const auto both = check_tvl1_reference(fixture, {.blend = 0}, 7);
    const auto protected_only = check_tvl1_reference(fixture, active_tvl1_parameters, 7);
    require(both.reason == methods::Tvl1Reason::zero_blend &&
                protected_only.reason == methods::Tvl1Reason::no_eligible_samples &&
                !protected_only.solver,
            "zero blend precedes E0 and both bypass solver");
    const CheckpointStop stop{core::Checkpoint::measurement, 0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto method = methods::Tvl1::create({.blend = 0}).value();
    std::optional<methods::Tvl1Stop> observed_stop;
    const auto invalid = methods::apply(method, {{}, {}, fixture.budget, scheduler}, observed_stop);
    require(!invalid && invalid.error().code == core::ErrorCode::argument,
            "malformed TV Working views outrank no-op and stop");
    const auto mask = fixture.protection.view().row(0);
    const auto mismatched =
        image::PlaneView<const std::uint8_t>::create(mask, {.width = 3, .height = 1, .stride = 3})
            .value();
    const auto wrong = methods::apply(
        method, {fixture.luminance.view(), mismatched, fixture.budget, scheduler}, observed_stop);
    require(!wrong && wrong.error().code == core::ErrorCode::argument,
            "mismatched mask axes outrank no-op and stop");
    const auto cancelled = run_tvl1(fixture, {.blend = 0}, scheduler);
    require(!cancelled && cancelled.error().code == core::ErrorCode::cancelled,
            "valid zero-blend Working operation still observes eligibility stop");
}
inline void tvl1_factory_cases() {
    for (const auto invalid : {0.01, 20.01, std::numeric_limits<double>::quiet_NaN()}) {
        require(!methods::Tvl1::create({.lambda = invalid, .blend = 0}),
                "zero blend cannot admit an invalid fidelity");
    }
    for (const auto cap : {9U, 1001U}) {
        require(!methods::Tvl1::create({.iterations = cap, .blend = 0}),
                "zero blend cannot admit an invalid iteration cap");
    }
    for (const auto tolerance : {0.0, 0.1, std::numeric_limits<double>::quiet_NaN()}) {
        require(!methods::Tvl1::create({.tolerance = tolerance, .blend = 0}),
                "zero blend cannot admit invalid tolerance");
    }
    for (const auto blend : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN()}) {
        require(!methods::Tvl1::create({.blend = blend}),
                "invalid blend remains a real typed factory refusal");
    }
    require(methods::Tvl1::create({.lambda = 0.05, .iterations = 10, .tolerance = 1e-8, .blend = 0})
                .has_value(),
            "healthy endpoint parameters with exact zero blend remain admitted");
}
inline void tvl1_resource_cases() {
    const auto plane = tvl1_plane_bytes(21, 3);
    Tvl1WorkingFixture exact{21, 3, 5 * plane};
    const auto resident = exact.budget.used();
    check_tvl1_reference(exact, active_tvl1_parameters, 7);
    require(exact.budget.peak() == resident + (5 * plane),
            "real fit peak includes all five aligned fields");
    Tvl1WorkingFixture short_budget{21, 3, (5 * plane) - 1};
    const auto before = gamma_bits(short_budget.luminance.view().storage());
    const auto charge = short_budget.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = run_tvl1(short_budget, active_tvl1_parameters, scheduler);
    require(!result && result.error().code == core::ErrorCode::resource &&
                short_budget.budget.peak() == charge + (4 * plane),
            "peak-minus-one reaches four actual fields then refuses the fifth acquisition");
    require(short_budget.budget.used() == charge &&
                gamma_bits(short_budget.luminance.view().storage()) == before,
            "fifth-field refusal refunds every acquisition before entering Y changes");
}
inline void tvl1_cancel_case(core::Checkpoint phase, std::size_t allowed,
                             bool protected_prefix = false) {
    Tvl1WorkingFixture fixture{2051, 1, 5 * tvl1_plane_bytes(2051, 1)};
    if (protected_prefix) {
        std::ranges::fill(fixture.protection.view().row(0).first(2050), std::uint8_t{255});
        std::ranges::fill(fixture.luminance.view().row(0).first(2050), -0.0);
    }
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto charge = fixture.budget.used();
    const CheckpointStop stop{phase, allowed};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result =
        run_tvl1(fixture, {.lambda = 0.05, .iterations = 10, .tolerance = 1e-8}, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == allowed + 1,
            "real requested allocation gather objective dual primal or map0 checkpoint is reached");
    require(fixture.budget.used() == charge &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "pre-map failure refunds joined fields and keeps all entering Y bits untouched");
}
inline void tvl1_map_stop_case(bool protected_prefix) {
    Tvl1WorkingFixture fixture{2051, 1, 5 * tvl1_plane_bytes(2051, 1)};
    if (protected_prefix) {
        std::ranges::fill(fixture.protection.view().row(0).first(2050), std::uint8_t{255});
        std::ranges::fill(fixture.luminance.view().row(0).first(2050), -0.0);
    }
    fixture.luminance.view().row(0).back() = 0.1;
    const methods::Tvl1Parameters parameters{
        .lambda = 0.05,
        .iterations = 10,
        .tolerance = 1e-8,
        .blend = 1,
    };
    const auto expected = tvl1_working_reference(fixture, parameters);
    require(expected.samples.at(2050) != fixture.luminance.view().row(0).back(),
            "late eligible x2050 would actually change without interior map cancellation");
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> original_mask{mask.begin(), mask.end()};
    const auto charge = fixture.budget.used();
    const CheckpointStop stop{core::Checkpoint::processing, 71};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = run_tvl1(fixture, parameters, scheduler);
    const auto after = gamma_bits(fixture.luminance.view().storage());
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == 72 && fixture.budget.used() == charge,
            "after ten7-poll iterations map stops at x1024 and refunds all fields");
    require(std::ranges::equal(std::span{after}.subspan(1024), std::span{before}.subspan(1024)) &&
                std::ranges::equal(mask, original_mask),
            "unstarted map suffix padding and complete mask retain exact bits");
    if (protected_prefix) {
        require(after == before,
                "protected map prefix still reaches its interior checkpoint before late change");
    } else {
        require(!std::ranges::equal(std::span{after}.first(1024), std::span{before}.first(1024)),
                "permitted private prefix contains actual TV-L1 stores");
    }
}
inline void tvl1_cancellation_cases() {
    for (const std::size_t allowed : {0U, 1U, 2U, 3U, 4U}) {
        tvl1_cancel_case(core::Checkpoint::allocation, allowed);
    }
    tvl1_cancel_case(core::Checkpoint::measurement, 5);
    tvl1_cancel_case(core::Checkpoint::measurement, 5, true);
    tvl1_cancel_case(core::Checkpoint::measurement, 8);
    tvl1_cancel_case(core::Checkpoint::measurement, 11);
    tvl1_cancel_case(core::Checkpoint::processing, 2);
    tvl1_cancel_case(core::Checkpoint::processing, 5);
    tvl1_cancel_case(core::Checkpoint::processing, 7);
    tvl1_cancel_case(core::Checkpoint::processing, 70);
    tvl1_map_stop_case(false);
    tvl1_map_stop_case(true);
}
inline void tvl1_working_cases() {
    tvl1_stop_cases();
    tvl1_active_cases();
    tvl1_precision_case();
    tvl1_objective_admission_case();
    tvl1_constant_cases();
    tvl1_high_lambda_case();
    tvl1_equal_decoded_write_case();
    tvl1_context_and_rounding_cases();
    tvl1_noop_and_admission_cases();
    tvl1_factory_cases();
    tvl1_resource_cases();
    tvl1_cancellation_cases();
}
} // namespace docenhance::tests
