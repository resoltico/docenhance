// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/morphology.hpp"
#include "docenhance/methods/morphology_working.hpp"
#include "docenhance/methods/surface.hpp"
#include "docenhance/methods/surface_working.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>
namespace docenhance::tests {
inline constexpr std::size_t illumination_boundary_budget = std::size_t{2} * 1024 * 1024;
inline void morphology_phase_boundary_cases() {
    GammaFixture source{32, 32};
    for (std::uint32_t y = 0; y < 32; ++y) {
        std::ranges::fill(source.luminance.view().row(y), .5);
    }
    const auto before = gamma_bits(source.luminance.view().storage());
    const auto method = methods::Morphology::create({.radius = 1});
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    require(method.has_value() && concurrency.has_value(), "I02 phase parameters admit");
    core::Budget budget{illumination_boundary_budget};
    for (const auto phase : {core::Checkpoint::allocation, core::Checkpoint::measurement}) {
        const CheckpointStop observe{phase, std::numeric_limits<std::size_t>::max()};
        const exec::Scheduler healthy{*concurrency, observe.cancellation()};
        const auto completed =
            methods::apply(*method, {source.luminance.view(), {}, budget, healthy});
        require(completed.has_value() && completed->analysis && completed->counts.evaluated == 1024,
                "healthy I02 actually fits, measures and maps once");
        const auto visits = CheckpointStop::visits();
        require(visits > 0 && budget.used() == 0, "I02 actual premap phase refunds");
        for (std::size_t allowed = 0; allowed < visits; ++allowed) {
            const CheckpointStop stop{phase, allowed};
            const exec::Scheduler scheduler{*concurrency, stop.cancellation()};
            const auto failed =
                methods::apply(*method, {source.luminance.view(), {}, budget, scheduler});
            require(!failed && failed.error().code == core::ErrorCode::cancelled &&
                        CheckpointStop::visits() == allowed + 1 && budget.used() == 0 &&
                        gamma_bits(source.luminance.view().storage()) == before,
                    "real I02 premap stop returns no Facts or stores and refunds every owner");
        }
    }
}
template <class Method>
void illumination_receiver_controls(const Method& method, GammaFixture& source, bool correction) {
    for (std::uint32_t y = 0; y < 32; ++y) {
        std::ranges::fill(source.luminance.view().row(y), .5);
        std::ranges::fill(source.protection.view().row(y), std::uint8_t{0});
    }
    source.protection.view().row(0).back() = 1;
    const auto before = gamma_bits(source.luminance.view().storage());
    const auto mask_before = source.protection.view().storage();
    const std::vector<std::uint8_t> mask(mask_before.begin(), mask_before.end());
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    require(concurrency.has_value(), "actual receiver concurrency admits");
    const exec::Scheduler scheduler{*concurrency};
    core::Budget budget{illumination_boundary_budget};
    const auto facts = methods::apply(
        method, {source.luminance.view(), source.protection.view().as_const(), budget, scheduler});
    require(facts.has_value() && facts->counts.evaluated == 1023 &&
                facts->counts.changed == (correction ? 1023U : 0U) &&
                source.luminance.view().row(0).back() == .5 &&
                (correction ? source.luminance.view().row(0).front() > .5
                            : gamma_bits(source.luminance.view().storage()) == before),
            "actual eager illumination attains identity/correction and preserves protected Y");
    const auto completed = *facts;
    const auto attained = gamma_bits(source.luminance.view().storage());
    const CheckpointStop stop{core::Checkpoint::processing, 0};
    const exec::Scheduler stopped{*concurrency, stop.cancellation()};
    const auto cancelled = methods::apply(
        method, {source.luminance.view(), source.protection.view().as_const(), budget, stopped});
    require(
        !cancelled && cancelled.error().code == core::ErrorCode::cancelled &&
            CheckpointStop::visits() == 1 && *facts == completed &&
            gamma_bits(source.luminance.view().storage()) == attained,
        "first actual processing stop returns no completed Facts and never changes retained Facts");
    const CheckpointStop malformed_stop{core::Checkpoint::measurement, 0};
    const exec::Scheduler malformed_scheduler{*concurrency, malformed_stop.cancellation()};
    const auto malformed = methods::apply(method, {{}, {}, budget, malformed_scheduler});
    require(!malformed && malformed.error().code == core::ErrorCode::argument &&
                CheckpointStop::visits() == 0 && budget.used() == 0 &&
                gamma_bits(source.luminance.view().storage()) == attained &&
                std::ranges::equal(mask, source.protection.view().storage()),
            "structural refusal precedes pending observation, all real phase owners refund and "
            "mask stays exact");
}
inline void illumination_receiver_case(std::optional<double> target) {
    GammaFixture source{32, 32};
    const auto morphology = methods::Morphology::create({.target = target, .radius = 1});
    const auto surface = methods::Surface::create({.target = target});
    require(morphology.has_value() && surface.has_value(),
            "real I01/I02 receiver parameters admit");
    illumination_receiver_controls(*morphology, source, target.has_value());
    illumination_receiver_controls(*surface, source, target.has_value());
}
inline void illumination_receiver_cases() {
    illumination_receiver_case(std::nullopt);
    illumination_receiver_case(.75);
}
} // namespace docenhance::tests
