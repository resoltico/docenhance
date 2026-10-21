// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/surface.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "surface_detail.hpp"
#include "surface_working_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
namespace docenhance::methods {
core::Result<void> admit_surface_coefficients(Working working,
                                              image::PlaneView<const double> logs) {
    const auto task = [&](std::size_t) -> core::Result<void> {
        const auto row = logs.row(0);
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i % surface_poll_interval == 0 &&
                working.scheduler.get().cancellation().requested(core::Checkpoint::solving)) {
                return core::cancelled();
            }
            if (!std::isfinite(row.subspan(i, 1).front())) {
                return core::failure(core::ErrorCode::numerical, "Nonfinite fitted log surface");
            }
        }
        return {};
    };
    return working.scheduler.get().for_each(1, exec::WorkRef{task});
}
core::Result<SurfaceFit> fit_surface(const Surface& method, Working working, SurfaceFacts facts,
                                     const SurfaceGrid& grid) {
    facts.resolved_cell = grid.cell;
    facts.cells = grid.columns * grid.rows;
    constexpr std::uint32_t workspace_rows = 7;
    auto work = surface_allocate<double>(working, facts.cells, workspace_rows);
    if (!work) {
        return std::unexpected(std::move(work.error()));
    }
    auto logs = surface_allocate<double>(working, facts.cells, 1);
    if (!logs) {
        return std::unexpected(std::move(logs.error()));
    }
    if (auto measured = measure_surface_cells(working, method, grid, work->view()); !measured) {
        return std::unexpected(std::move(measured.error()));
    }
    if (auto excluded =
            exclude_surface_dark_cells(work->view(), facts.background_reference, facts.dark_cells,
                                       working.scheduler.get().cancellation());
        !excluded) {
        return std::unexpected(std::move(excluded.error()));
    }
    facts.measured_cells = static_cast<std::uint32_t>(
        std::ranges::count_if(work->view().row(0), [](double weight) { return weight > 0; }));
    facts.coverage = surface_coverage(facts.measured_cells, facts.cells);
    facts.auto_predicates.coverage = *facts.coverage >= surface_automatic_coverage;
    const bool automatic = method.parameters().mode == SurfaceMode::automatic;
    const auto required = automatic ? surface_automatic_coverage : surface_explicit_coverage;
    if (facts.measured_cells == 0 || *facts.coverage < required) {
        if (!automatic) {
            return core::failure(core::ErrorCode::method_inapplicable,
                                 "Insufficient eligible I01 measurements");
        }
        facts.disposition = Disposition::skipped;
        facts.reason = SurfaceReason::insufficient_cells;
        return SurfaceFit{.grid = grid, .logarithms = {}, .facts = facts};
    }
    SolverReport report;
    const SurfaceSystem system{
        .grid = grid,
        .weights = work->view().row(0),
        .smooth = method.parameters().smooth,
    };
    const auto cancellation = working.scheduler.get().cancellation();
    const auto task = [&](std::size_t) {
        return solve_surface(system, work->view(), logs->view().row(0), report,
                             {.budget = working.budget.get(), .cancellation = cancellation});
    };
    if (auto solved = working.scheduler.get().for_each(1, exec::WorkRef{task}); !solved) {
        return std::unexpected(std::move(solved.error()));
    }
    facts.solver = report;
    if (auto admitted = admit_surface_coefficients(working, logs->view().as_const()); !admitted) {
        return std::unexpected(std::move(admitted.error()));
    }
    return SurfaceFit{.grid = grid, .logarithms = std::move(*logs), .facts = facts};
}
} // namespace docenhance::methods
