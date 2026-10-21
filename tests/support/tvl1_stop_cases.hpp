// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/methods/tvl1_working.hpp"
#include "docenhance/methods/working.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "tvl1_working_fixture.hpp"
#include "tvl1_working_reference.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace docenhance::tests {
inline methods::Working stop_working(Tvl1WorkingFixture& fixture,
                                     const exec::Scheduler& scheduler) {
    return {fixture.luminance.view(), {}, fixture.budget, scheduler};
}
inline core::Error tvl1_stop_objective_case(std::uint32_t cap, std::size_t allowed,
                                            std::optional<methods::Tvl1Stop> expected) {
    Tvl1WorkingFixture fixture{1, 1, 5 * tvl1_plane_bytes(1, 1)};
    fixture.luminance.view().row(0).front() = 0.4;
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto resident = fixture.budget.used();
    const CheckpointStop stop{core::Checkpoint::measurement, allowed};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    std::optional<methods::Tvl1Stop> observed = methods::Tvl1Stop::iteration_limit;
    const auto method = methods::Tvl1::create({.iterations = cap}).value();
    const auto result = methods::apply(method, stop_working(fixture, scheduler), observed);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == allowed + 1,
            "real Working start/final objective observes the exact requested boundary");
    require(observed == expected && fixture.budget.used() == resident &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "actual optional stop survives failure while joined fields refund and Y stays exact");
    return result.error();
}
inline void tvl1_stop_objective_cases() {
    const auto early = tvl1_stop_objective_case(10, 2, std::nullopt);
    const auto late = tvl1_stop_objective_case(10, 3, methods::Tvl1Stop::iteration_limit);
    require(early.code == late.code && early.message == late.message,
            "same request and same error cannot replace the actual stop observation");
    tvl1_stop_objective_case(30, 3, methods::Tvl1Stop::tolerance_met);
    for (const auto cap : {10U, 30U}) {
        Tvl1WorkingFixture fixture{1, 1, 5 * tvl1_plane_bytes(1, 1)};
        fixture.luminance.view().row(0).front() = 0.4;
        const auto resident = fixture.budget.used();
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
        std::optional<methods::Tvl1Stop> observed;
        const auto method = methods::Tvl1::create({.iterations = cap}).value();
        const auto result = methods::apply(method, stop_working(fixture, scheduler), observed);
        require(result && result->solver && methods::validate(*result, method.parameters()),
                "same healthy request returns actual admitted completed solver Facts");
        if (!result || !result->solver) {
            return;
        }
        require(observed == result->solver->stop && result->solver->iterations == cap &&
                    result->solver->objective_start == 0 && result->solver->objective_end == 0 &&
                    fixture.budget.used() == resident,
                "success retains observed stop through caller admission and refunds fit fields");
    }
}
inline void tvl1_seed_stop(std::optional<methods::Tvl1Stop>& observed) {
    Tvl1WorkingFixture fixture{1, 1, 5 * tvl1_plane_bytes(1, 1)};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = methods::apply(methods::Tvl1::create({.iterations = 10}).value(),
                                       stop_working(fixture, scheduler), observed);
    require(result && result->solver && observed == methods::Tvl1Stop::iteration_limit,
            "reused caller slot comes from a real successful prior execution");
}
inline void tvl1_stop_reset_cases() {
    Tvl1WorkingFixture fixture{1, 1, 0};
    const auto resident = fixture.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto active = methods::Tvl1::create({.iterations = 10}).value();
    std::optional<methods::Tvl1Stop> observed;
    tvl1_seed_stop(observed);
    const auto invalid = methods::apply(active, {{}, {}, fixture.budget, scheduler}, observed);
    require(!invalid && invalid.error().code == core::ErrorCode::argument && !observed,
            "entry resets a stale observation before real geometry refusal");
    tvl1_seed_stop(observed);
    const auto refused = methods::apply(active, stop_working(fixture, scheduler), observed);
    require(!refused && refused.error().code == core::ErrorCode::resource && !observed &&
                fixture.budget.used() == resident,
            "allocation before a stop is known leaves no stale cap and refunds");
    tvl1_seed_stop(observed);
    const auto zero = methods::Tvl1::create({.blend = 0}).value();
    const auto bypassed = methods::apply(zero, stop_working(fixture, scheduler), observed);
    require(bypassed && !bypassed->solver && !observed && fixture.budget.used() == resident,
            "zero blend resets reuse without allocating or inventing a stop");
    tvl1_seed_stop(observed);
    fixture.protection.view().row(0).front() = 255;
    const auto protected_only = methods::apply(
        active,
        {fixture.luminance.view(), fixture.protection.view().as_const(), fixture.budget, scheduler},
        observed);
    require(protected_only && !protected_only->solver && !observed &&
                fixture.budget.used() == resident,
            "no eligible samples reset reuse without solving");
}
inline void tvl1_stop_map_case(std::size_t allowed) {
    Tvl1WorkingFixture fixture{2051, 1, 5 * tvl1_plane_bytes(2051, 1)};
    fixture.luminance.view().row(0).back() = 0.1;
    const methods::Tvl1Parameters parameters{
        .lambda = 0.05,
        .iterations = 10,
        .tolerance = 1e-8,
        .blend = 1,
    };
    const auto expected = tvl1_working_reference(fixture, parameters);
    require(expected.samples.at(2050) != fixture.luminance.view().row(0).back(),
            "late eligible sample would actually change without map cancellation");
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto resident = fixture.budget.used();
    const CheckpointStop stop{core::Checkpoint::processing, allowed};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    std::optional<methods::Tvl1Stop> observed;
    const auto result = methods::apply(methods::Tvl1::create(parameters).value(),
                                       stop_working(fixture, scheduler), observed);
    require(!result && result.error().code == core::ErrorCode::cancelled &&
                CheckpointStop::visits() == allowed + 1 &&
                observed == methods::Tvl1Stop::iteration_limit && fixture.budget.used() == resident,
            "actual post-solve map failure retains cap and refunds every field");
    const auto after = gamma_bits(fixture.luminance.view().storage());
    if (allowed == 70) {
        require(after == before, "map entry cancellation changes no sample");
    } else {
        require(
            !std::ranges::equal(std::span{after}.first(1024), std::span{before}.first(1024)) &&
                std::ranges::equal(std::span{after}.subspan(1024), std::span{before}.subspan(1024)),
            "map interior preserves real changed prefix and exact unstarted suffix/padding");
    }
}
inline void tvl1_stop_cases() {
    tvl1_stop_objective_cases();
    tvl1_stop_reset_cases();
    tvl1_stop_map_case(70);
    tvl1_stop_map_case(71);
}
} // namespace docenhance::tests
