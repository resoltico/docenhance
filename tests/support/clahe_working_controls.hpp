// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "clahe_working_cases.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/clahe.hpp"
#include "require.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace docenhance::tests {
inline auto run_clahe(ClaheFixture& fixture, methods::ClaheParameters parameters,
                      const exec::Scheduler& scheduler) {
    return methods::apply(methods::Clahe::create(parameters).value(),
                          {fixture.luminance.view(), fixture.protection.view().as_const(),
                           fixture.budget, scheduler});
}
inline void clahe_noop_cases() {
    ClaheFixture tiny{1, 1, 0};
    const auto zero = check_clahe_reference(tiny, {.blend = 0}, 7);
    require(zero.reason == methods::ClaheReason::zero_blend && zero.identity_tiles == 0,
            "valid zero blend bypasses pixel-dependent tile applicability without workspace");
    tiny.protection.view().row(0).front() = 255;
    const auto both = check_clahe_reference(tiny, {.blend = 0}, 7);
    const auto protected_only = check_clahe_reference(tiny, small_clahe_parameters, 7);
    require(both.reason == methods::ClaheReason::zero_blend &&
                protected_only.reason == methods::ClaheReason::no_eligible_samples &&
                protected_only.identity_tiles == 0,
            "zero blend precedes E0 and both bypass map fitting and tile size");
}
inline void clahe_admission_cases() {
    ClaheFixture fixture{67, 51, clahe_scratch_bytes(12)};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto refused = run_clahe(fixture, {.grid_columns = 3, .grid_rows = 4}, scheduler);
    require(!refused && refused.error().code == core::ErrorCode::argument &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "67x51 grid3x4 refuses12-pixel height despite enough total area");
    const CheckpointStop stop{core::Checkpoint::measurement, 0};
    const exec::Scheduler stopped{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                  stop.cancellation()};
    const auto method = methods::Clahe::create({.blend = 0}).value();
    const auto malformed = methods::apply(method, {{}, {}, fixture.budget, stopped});
    require(!malformed && malformed.error().code == core::ErrorCode::argument,
            "malformed Working geometry precedes valid no-op and pending stop");
    const auto wrong = fixture.protection.view().row(0);
    const auto wrong_view = image::PlaneView<const std::uint8_t>::create(
                                wrong, {.width = 67, .height = 1, .stride = 67})
                                .value();
    const auto mismatched =
        methods::apply(method, {fixture.luminance.view(), wrong_view, fixture.budget, stopped});
    require(!mismatched && mismatched.error().code == core::ErrorCode::argument,
            "mismatched mask axes precede no-op and pending stop");
    const auto cancelled = run_clahe(fixture, {.grid_columns = 3, .grid_rows = 4}, stopped);
    require(!cancelled && cancelled.error().code == core::ErrorCode::cancelled,
            "pending eligibility stop precedes active tile applicability");
    const auto cancelled_noop = run_clahe(fixture, {.blend = 0}, stopped);
    require(!cancelled_noop && cancelled_noop.error().code == core::ErrorCode::cancelled,
            "valid zero blend still observes eligibility cancellation before its no-op");
}
inline void clahe_resource_cases() {
    const auto scratch = clahe_scratch_bytes(4);
    ClaheFixture exact{33, 35, scratch};
    const auto resident = exact.budget.used();
    check_clahe_reference(exact, small_clahe_parameters, 7);
    require(exact.budget.peak() == resident + scratch,
            "real peak includes overlapping aligned knots and statistics");
    ClaheFixture short_budget{33, 35, scratch - 1};
    const auto before = gamma_bits(short_budget.luminance.view().storage());
    const auto charge = short_budget.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto refused = run_clahe(short_budget, small_clahe_parameters, scheduler);
    require(!refused && refused.error().code == core::ErrorCode::resource &&
                short_budget.budget.peak() == charge + clahe_plane_bytes(1025, 4),
            "peak-minus-one reaches first map allocation then refuses actual statistics");
    require(short_budget.budget.used() == charge &&
                gamma_bits(short_budget.luminance.view().storage()) == before,
            "second-allocation refusal refunds maps and leaves input untouched");
}
inline void clahe_cancel_case(core::Checkpoint phase, std::size_t allowed) {
    ClaheFixture fixture{32, 32, clahe_scratch_bytes(4)};
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto charge = fixture.budget.used();
    const CheckpointStop stop{phase, allowed};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = run_clahe(fixture, small_clahe_parameters, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == allowed + 1,
            "actual requested CLAHE phase boundary is reached without completed Facts");
    require(fixture.budget.used() == charge, "cancelled CLAHE joins and refunds all method planes");
    if (phase != core::Checkpoint::processing || allowed == 0) {
        require(gamma_bits(fixture.luminance.view().storage()) == before,
                "all pre-map and initial-map stops preserve Y bits");
    } else {
        const auto view = fixture.luminance.view();
        require(gamma_bits(view.row(0)) !=
                    std::vector<std::uint64_t>{before.begin(), before.begin() + 32},
                "the permitted first row contains actual writes");
        const auto first_unstarted = view.row_pitch();
        const auto after = gamma_bits(view.storage());
        require(std::equal(after.begin() + static_cast<std::ptrdiff_t>(first_unstarted),
                           after.end(),
                           before.begin() + static_cast<std::ptrdiff_t>(first_unstarted)),
                "stopped mapping preserves every unstarted row and padding bit");
    }
}
inline void clahe_wide_stop_case() {
    ClaheFixture fixture{2051, 32, clahe_scratch_bytes(4)};
    for (std::uint32_t x = 0; x < 2050; ++x) {
        fixture.protection.view().row(0).subspan(x, 1).front() = 255;
        fixture.luminance.view().row(0).subspan(x, 1).front() = -0.0;
    }
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto charge = fixture.budget.used();
    const CheckpointStop stop{core::Checkpoint::measurement, 102};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = run_clahe(fixture, small_clahe_parameters, scheduler);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == 103 && fixture.budget.used() == charge &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "gather polls inside a long protected row at1024 before fitting or writes");
}
inline void clahe_wide_processing_stop_case(bool protected_prefix) {
    ClaheFixture fixture{2051, 32, clahe_scratch_bytes(4)};
    if (protected_prefix) {
        for (std::uint32_t x = 0; x < 2050; ++x) {
            fixture.protection.view().row(0).subspan(x, 1).front() = 255;
            fixture.luminance.view().row(0).subspan(x, 1).front() = -0.0;
        }
    }
    const auto expected = clahe_reference(fixture, small_clahe_parameters);
    require(expected.samples.at(2050) != fixture.luminance.view().row(0).back(),
            "eligible row0 x2050 beyond cutoff would actually change without the interior stop");
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> original_mask{mask.begin(), mask.end()};
    const auto charge = fixture.budget.used();
    const CheckpointStop stop{core::Checkpoint::processing, 1};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = run_clahe(fixture, small_clahe_parameters, scheduler);
    const auto after = gamma_bits(fixture.luminance.view().storage());
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == 2 && fixture.budget.used() == charge,
            "actual map processing stops at row0 x1024 and refunds joined method storage");
    require(std::ranges::equal(std::span{after}.subspan(1024), std::span{before}.subspan(1024)) &&
                std::ranges::equal(mask, original_mask),
            "interior processing stop preserves all unstarted destinations padding and masks");
    if (protected_prefix) {
        require(after == before, "long protected map prefix still reaches its interior checkpoint");
    } else {
        require(!std::ranges::equal(std::span{after}.first(1024), std::span{before}.first(1024)),
                "interior map stop retains an actually changed allowed prefix");
    }
}
inline void clahe_cancellation_cases() {
    clahe_cancel_case(core::Checkpoint::allocation, 0);
    clahe_cancel_case(core::Checkpoint::allocation, 1);
    for (const std::size_t allowed : {33U, 36U, 37U, 68U, 69U, 72U}) {
        clahe_cancel_case(core::Checkpoint::measurement, allowed);
    }
    clahe_cancel_case(core::Checkpoint::processing, 0);
    clahe_cancel_case(core::Checkpoint::processing, 1);
    clahe_wide_stop_case();
    clahe_wide_processing_stop_case(false);
    clahe_wide_processing_stop_case(true);
}
inline void clahe_working_cases() {
    clahe_active_cases();
    clahe_identity_cases();
    for (const auto count : {15U, 16U}) {
        clahe_cutoff_case(count, 1.0 / 8192);
        clahe_cutoff_case(count, 1.0 / 4096);
    }
    clahe_white_write_case();
    clahe_noop_cases();
    clahe_admission_cases();
    clahe_resource_cases();
    clahe_cancellation_cases();
}
} // namespace docenhance::tests
