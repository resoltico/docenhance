// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/surface.hpp"
#include "docenhance/methods/surface_working.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"
#include "surface_working_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
namespace docenhance::tests {
inline constexpr std::size_t surface_source_budget = std::size_t{2} * 1024 * 1024;
inline void surface_phase_stop_case(core::Checkpoint phase) {
    GammaFixture source{32, 32};
    for (std::uint32_t y = 0; y < 32; ++y) {
        std::ranges::fill(source.luminance.view().row(y), .5);
    }
    const auto before = gamma_bits(source.luminance.view().storage());
    const auto method = methods::Surface::create({.target = .75, .cell = 8});
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    require(method.has_value() && concurrency.has_value(), "I01 phase parameters admit");
    core::Budget budget{surface_source_budget};
    const CheckpointStop observe{phase, std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler healthy{*concurrency, observe.cancellation()};
    const auto completed = methods::apply(*method, {source.luminance.view(), {}, budget, healthy});
    require(completed.has_value() && completed->counts.changed == 1024,
            "healthy eager I01 actually maps all eligible samples");
    const auto visits = CheckpointStop::visits();
    require(visits > 0 && budget.used() == 0, "actual I01 phase is attained and refunded");
    for (std::size_t allowed = 0; allowed < visits; ++allowed) {
        for (std::uint32_t y = 0; y < 32; ++y) {
            std::ranges::fill(source.luminance.view().row(y), .5);
        }
        const CheckpointStop stop{phase, allowed};
        const exec::Scheduler scheduler{*concurrency, stop.cancellation()};
        const auto failed =
            methods::apply(*method, {source.luminance.view(), {}, budget, scheduler});
        require(!failed && failed.error().code == core::ErrorCode::cancelled &&
                    CheckpointStop::visits() == allowed + 1 && budget.used() == 0 &&
                    gamma_bits(source.luminance.view().storage()) == before,
                "actual premap I01 phase stop returns no Facts, leaves Y exact and refunds");
    }
}
inline void surface_source_cases() {
    GammaFixture source{32, 32};
    for (std::uint32_t y = 0; y < 32; ++y) {
        std::ranges::fill(source.luminance.view().row(y), .5);
    }
    const auto method = methods::Surface::create({.cell = 8});
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    require(method.has_value() && concurrency.has_value(), "I01 owned fit parameters admit");
    const exec::Scheduler scheduler{*concurrency};
    core::Budget budget{surface_source_budget};
    const auto before = gamma_bits(source.luminance.view().storage());
    const methods::Working working{source.luminance.view(), {}, budget, scheduler};
    const auto counts = methods::eligibility(working);
    const auto grid = methods::surface_grid({.width = 32, .height = 32}, *method);
    require(counts.has_value() && grid.has_value(), "actual I01 shape and cohort admit");
    {
        const auto fit = methods::fit_surface(*method, working, {.counts = *counts}, *grid);
        require(fit.has_value() && fit->facts.measured_cells == 16 && fit->facts.solver &&
                    budget.used() > 0 && gamma_bits(source.luminance.view().storage()) == before,
                "actual I01 fit retains charged coefficients and actual cell/solver Facts");
    }
    require(budget.used() == 0, "fitted I01 coefficient owner destruction refunds");
    for (const auto phase :
         {core::Checkpoint::allocation, core::Checkpoint::measurement, core::Checkpoint::solving}) {
        surface_phase_stop_case(phase);
    }
}
} // namespace docenhance::tests
