// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "surface_working_cases.hpp"
#include "surface_working_fixture.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
namespace docenhance::tests {
inline void surface_grid_noop_case() {
    SurfaceWorkingFixture fixture{1048576, 1, 0};
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto zero = run_surface(fixture, {.strength = 0, .cell = 8}, scheduler);
    const auto unit = run_surface(fixture, {.max_gain = 1, .cell = 8}, scheduler);
    const auto active = run_surface(fixture, {.cell = 8}, scheduler);
    std::ranges::fill(fixture.protection.view().storage(), std::uint8_t{255});
    const auto empty = run_surface(fixture, {.cell = 8}, scheduler);
    require(zero && zero->reason == methods::SurfaceReason::zero_strength && unit &&
                unit->reason == methods::SurfaceReason::unit_gain && empty &&
                empty->reason == methods::SurfaceReason::no_eligible_samples && !active &&
                active.error().code == core::ErrorCode::resource &&
                fixture.budget.peak() == fixture.budget.used() &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "physical over-limit grid admits ordered no-ops before active grid refusal with no "
            "scratch");
}
inline void surface_hierarchy_working_case() {
    // C1024,L8192,W57344; Q32768; actual G1365,H65664. Hierarchy peak=131200.
    for (const auto scratch : {std::size_t{131200}, std::size_t{131199}}) {
        SurfaceWorkingFixture fixture{256, 256, scratch};
        std::ranges::fill(fixture.protection.view().storage(), std::uint8_t{255});
        for (std::uint32_t y = 0; y < 64; ++y) {
            if (y % 8 < 2) {
                std::ranges::fill(fixture.protection.view().row(y), std::uint8_t{0});
            }
        }
        const auto held = fixture.budget.used();
        const auto before = gamma_bits(fixture.luminance.view().storage());
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
        const auto result = run_surface(fixture, {.target = 0.8, .cell = 8}, scheduler);
        require(fixture.budget.used() == held, "actual conditional hierarchy owner refunds");
        if (scratch == 131200) {
            require(result && result->solver && result->solver->iterations > 0 &&
                        fixture.budget.peak() == held + scratch,
                    "actual Working fit attains conditional hierarchy dominating aligned peak");
        } else {
            require(
                !result && result.error().code == core::ErrorCode::resource &&
                    fixture.budget.peak() == held + 98304 &&
                    gamma_bits(fixture.luminance.view().storage()) == before,
                "one-byte-short hierarchy acquisition refuses after real cell phase before map");
        }
    }
}
inline void surface_prefix_case(bool protected_prefix) {
    SurfaceWorkingFixture healthy{1025, 1, surface_test_scratch};
    SurfaceWorkingFixture stopped{1025, 1, surface_test_scratch};
    for (auto* const fixture : {&healthy, &stopped}) {
        std::ranges::fill(fixture->luminance.view().row(0), 0.25);
        fixture->luminance.view().row(0).back() = 0.1;
        if (protected_prefix) {
            std::ranges::fill(fixture->protection.view().row(0).first(512), std::uint8_t{255});
        }
    }
    const auto before = gamma_bits(stopped.luminance.view().storage());
    const methods::SurfaceParameters p{.max_gain = 4, .target = 0.75, .cell = 512};
    const exec::Scheduler normal{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto completed = run_surface(healthy, p, normal);
    require(completed && completed->counts.changed > 0 &&
                healthy.luminance.view().row(0).back() > 0.1,
            "healthy actual fitted field proves a later destination WOULD change");
    const auto held = stopped.budget.used();
    const CheckpointStop stop{core::Checkpoint::processing, 1};
    const exec::Scheduler schedule{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                   stop.cancellation()};
    const auto result = run_surface(stopped, p, schedule);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == 2 && stopped.budget.used() == held,
            "interior actual map stop joins and refunds without completed Facts");
    for (std::size_t i = 0; i < stopped.luminance.view().storage().size(); ++i) {
        const auto value = stopped.luminance.view().storage().subspan(i, 1).front();
        if (i < 256 && !protected_prefix) {
            require(value == healthy.luminance.view().row(0).subspan(i, 1).front() &&
                        std::bit_cast<std::uint64_t>(value) != before.at(i),
                    "actual unprotected prefix stores equal healthy output");
        } else {
            require(std::bit_cast<std::uint64_t>(value) == before.at(i),
                    "protected prefix and complete later suffix/padding retain exact input bits");
        }
    }
}
} // namespace docenhance::tests
