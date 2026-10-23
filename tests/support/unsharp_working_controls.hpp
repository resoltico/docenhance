// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/unsharp.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "unsharp_fixture.hpp"
#include "unsharp_reference.hpp"
#include "unsharp_working_cases.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace docenhance::tests {
inline auto run_unsharp(UnsharpFixture& fixture, methods::UnsharpParameters parameters,
                        const exec::Scheduler& scheduler) {
    return methods::apply(methods::Unsharp::create(parameters).value(),
                          {fixture.luminance.view(), fixture.protection.view().as_const(),
                           fixture.budget, scheduler});
}
inline void unsharp_noop_cases() {
    UnsharpFixture fixture{3, 2, 0};
    const auto zero = check_unsharp_reference(fixture, {.amount = 0}, 7);
    require(zero.reason == methods::UnsharpReason::zero_amount && !zero.pre_clamp &&
                zero.context_samples == 0,
            "zero amount needs no Gaussian fields or observations");
    for (std::uint32_t y = 0; y < 2; ++y) {
        std::ranges::fill(fixture.protection.view().row(y), std::uint8_t{255});
    }
    const auto both = check_unsharp_reference(fixture, {.amount = 0}, 7);
    const auto protected_only = check_unsharp_reference(fixture, default_unsharp_parameters, 7);
    require(both.reason == methods::UnsharpReason::zero_amount &&
                protected_only.reason == methods::UnsharpReason::no_eligible_samples &&
                protected_only.context_samples == 0,
            "zero amount precedes E0 and protection-only work uses no fields");
}
inline void unsharp_admission_cases() {
    UnsharpFixture fixture{3, 2, 0};
    const CheckpointStop stop{core::Checkpoint::measurement, 0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto method = methods::Unsharp::create({.amount = 0}).value();
    const auto malformed = methods::apply(method, {{}, {}, fixture.budget, scheduler});
    require(!malformed && malformed.error().code == core::ErrorCode::argument,
            "invalid Working geometry outranks no-op and observed stop");
    const auto mask = fixture.protection.view().row(0);
    const auto mismatched =
        image::PlaneView<const std::uint8_t>::create(mask, {.width = 3, .height = 1, .stride = 3})
            .value();
    const auto wrong =
        methods::apply(method, {fixture.luminance.view(), mismatched, fixture.budget, scheduler});
    require(!wrong && wrong.error().code == core::ErrorCode::argument,
            "mismatched protection axes outrank no-op and pending stop");
    const auto cancelled = run_unsharp(fixture, {.amount = 0}, scheduler);
    require(!cancelled && cancelled.error().code == core::ErrorCode::cancelled,
            "valid zero amount still observes eligibility cancellation");
}
inline void unsharp_resource_cases() {
    const auto field = unsharp_plane_bytes(21, 3);
    UnsharpFixture exact{21, 3, 2 * field};
    const auto resident = exact.budget.used();
    check_unsharp_reference(exact, default_unsharp_parameters, 7);
    require(exact.budget.peak() == resident + (2 * field),
            "actual fit peak includes both aligned Gaussian fields");
    UnsharpFixture short_budget{21, 3, (2 * field) - 1};
    const auto before = gamma_bits(short_budget.luminance.view().storage());
    const auto charge = short_budget.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = run_unsharp(short_budget, default_unsharp_parameters, scheduler);
    require(!result && result.error().code == core::ErrorCode::resource &&
                short_budget.budget.peak() == charge + field,
            "peak-minus-one reaches first actual field then refuses second");
    require(short_budget.budget.used() == charge &&
                gamma_bits(short_budget.luminance.view().storage()) == before,
            "second field refusal refunds and does not mutate entering Y");
}
inline void unsharp_protect_prefix(UnsharpFixture& fixture) {
    const auto count = fixture.luminance.width() - 1;
    std::ranges::fill(fixture.protection.view().row(0).first(count), std::uint8_t{255});
    std::ranges::fill(fixture.luminance.view().row(0).first(count), -0.0);
}
inline void unsharp_cancel_case(core::Checkpoint phase, std::size_t allowed,
                                bool protected_prefix = false) {
    UnsharpFixture fixture{2051, 1, 2 * unsharp_plane_bytes(2051, 1)};
    if (protected_prefix) {
        unsharp_protect_prefix(fixture);
    }
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto charge = fixture.budget.used();
    const CheckpointStop stop{phase, allowed};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = run_unsharp(fixture, default_unsharp_parameters, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == allowed + 1,
            "actual requested allocation gather or Gaussian processing checkpoint is reached");
    require(fixture.budget.used() == charge &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "pre-map cancellation refunds both fields and keeps Y unchanged");
}
inline void unsharp_map_stop_case(bool protected_prefix) {
    UnsharpFixture fixture{2051, 1, 2 * unsharp_plane_bytes(2051, 1)};
    if (protected_prefix) {
        unsharp_protect_prefix(fixture);
    }
    fixture.luminance.view().row(0).back() = 0.1;
    const auto expected = unsharp_reference(fixture, default_unsharp_parameters);
    require(expected.samples.at(2050) != fixture.luminance.view().row(0).back(),
            "late eligible x2050 would actually change without the interior map poll");
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> original_mask{mask.begin(), mask.end()};
    const auto charge = fixture.budget.used();
    const CheckpointStop stop{core::Checkpoint::processing, 35};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = run_unsharp(fixture, default_unsharp_parameters, scheduler);
    const auto after = gamma_bits(fixture.luminance.view().storage());
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == 36 && fixture.budget.used() == charge,
            "after both17-poll Gaussian passes mapping stops at x1024 and refunds");
    require(std::ranges::equal(std::span{after}.subspan(1024), std::span{before}.subspan(1024)) &&
                std::ranges::equal(mask, original_mask),
            "unstarted map suffix padding and complete mask remain bit-exact");
    if (protected_prefix) {
        require(after == before, "long protected map prefix cannot skip its interior checkpoint");
    } else {
        require(!std::ranges::equal(std::span{after}.first(1024), std::span{before}.first(1024)),
                "permitted private map prefix contains actual writes");
    }
}
inline void unsharp_cancellation_cases() {
    unsharp_cancel_case(core::Checkpoint::allocation, 0);
    unsharp_cancel_case(core::Checkpoint::allocation, 1);
    unsharp_cancel_case(core::Checkpoint::measurement, 5);
    unsharp_cancel_case(core::Checkpoint::measurement, 5, true);
    unsharp_cancel_case(core::Checkpoint::processing, 1);
    unsharp_cancel_case(core::Checkpoint::processing, 18);
    unsharp_cancel_case(core::Checkpoint::processing, 34);
    unsharp_map_stop_case(false);
    unsharp_map_stop_case(true);
}
inline void unsharp_working_cases() {
    unsharp_active_cases();
    unsharp_constant_and_rounding_cases();
    unsharp_endpoint_case();
    unsharp_context_case();
    unsharp_noop_cases();
    unsharp_admission_cases();
    unsharp_resource_cases();
    unsharp_cancellation_cases();
}
} // namespace docenhance::tests
