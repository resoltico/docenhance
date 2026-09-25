// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <utility>

namespace docenhance::methods {
namespace {
struct FitContext {
    SurfaceInput input;
    std::reference_wrapper<const Surface> method;
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const core::Cancellation> cancellation;
    std::reference_wrapper<IlluminationReport> report;
};
core::Result<std::uint64_t> count_row(std::span<const std::uint8_t> row,
                                      const core::Cancellation& cancellation) {
    std::uint64_t count = 0;
    for (std::size_t x = 0; x < row.size(); ++x) {
        if (x % surface_poll_interval == 0 &&
            cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        count += static_cast<std::uint64_t>(surface_at(row, x) != 0);
    }
    return count;
}
core::Result<void> count_eligible(FitContext context) {
    const auto& input = context.input;
    auto& report = context.report.get();
    const auto extent = input.source.get().extent();
    if (extent.width == 0 || extent.height == 0 ||
        (!input.protection.empty() && (input.protection.width() != extent.width ||
                                       input.protection.height() != extent.height))) {
        return core::failure(core::ErrorCode::argument, "I01 source and protection extents differ");
    }
    if (!input.protection.empty()) {
        for (std::uint32_t y = 0; y < extent.height; ++y) {
            auto count = count_row(input.protection.row(y), context.cancellation.get());
            if (!count) {
                return std::unexpected(count.error());
            }
            report.protected_samples += *count;
        }
    }
    report.eligible_samples =
        (std::uint64_t{extent.width} * extent.height) - report.protected_samples;
    return {};
}
core::Result<bool> applicable(bool enough, SurfaceReason reason, FitContext context) {
    if (enough) {
        return true;
    }
    auto& report = context.report.get();
    report.reason = reason;
    if (context.method.get().parameters().mode == SurfaceMode::automatic) {
        report.status = SurfaceStatus::skipped;
        return false;
    }
    return core::failure(core::ErrorCode::method_inapplicable,
                         "Insufficient eligible I01 measurements");
}
core::Result<bool> should_fit(FitContext context) {
    auto& report = context.report.get();
    const auto& parameters = context.method.get().parameters();
    if (report.eligible_samples == 0) {
        report.reason = SurfaceReason::no_eligible_samples;
    } else if (parameters.strength == 0) {
        report.reason = SurfaceReason::zero_strength;
    } else if (parameters.max_gain == 1) {
        report.reason = SurfaceReason::unit_gain;
    }
    if (report.reason != SurfaceReason::none) {
        report.status = SurfaceStatus::no_change;
        return false;
    }
    constexpr std::uint64_t minimum_samples = 16;
    const auto extent = context.input.source.get().extent();
    return applicable(report.eligible_samples >=
                          std::min(minimum_samples, std::uint64_t{extent.width} * extent.height),
                      SurfaceReason::insufficient_samples, context);
}
core::Result<image::Plane<double>> fit_grid(const SurfaceGrid& grid, FitContext context) {
    auto& report = context.report.get();
    report.cell = grid.cell;
    report.cells = grid.columns * grid.rows;
    constexpr unsigned workspace_rows = 7;
    auto work = image::Plane<double>::allocate(context.budget.get(), report.cells, workspace_rows);
    if (!work) {
        return std::unexpected(work.error());
    }
    auto logs = image::Plane<double>::allocate(context.budget.get(), report.cells, 1);
    if (!logs) {
        return std::unexpected(logs.error());
    }
    const MeasurementContext measurement{
        .method = context.method,
        .budget = context.budget,
        .cancellation = context.cancellation,
    };
    auto measured = measure_cells(context.input, grid, work->view(), measurement);
    if (!measured) {
        return std::unexpected(measured.error());
    }
    report.measured_cells = static_cast<std::uint32_t>(
        std::ranges::count_if(work->view().row(0), [](double w) { return w > 0; }));
    constexpr double explicit_coverage = 0.25;
    constexpr double automatic_coverage = 0.60;
    const double coverage = static_cast<double>(report.measured_cells) / report.cells;
    const bool automatic = context.method.get().parameters().mode == SurfaceMode::automatic;
    report.predicates.front() = coverage >= automatic_coverage;
    const auto covered =
        applicable(report.measured_cells != 0 &&
                       coverage >= (automatic ? automatic_coverage : explicit_coverage),
                   SurfaceReason::insufficient_cells, context);
    if (!covered) {
        return std::unexpected(covered.error());
    }
    if (!*covered) {
        return image::Plane<double>{};
    }
    report.solver.emplace();
    const SurfaceSystem system{
        .grid = grid,
        .weights = work->view().row(0),
        .smooth = context.method.get().parameters().smooth,
    };
    auto solved = solve_surface(system, work->view(), logs->view().row(0), *report.solver,
                                context.cancellation.get());
    if (!solved) {
        return std::unexpected(solved.error());
    }
    return std::move(*logs); // Work and selection scratch are released before lattice allocation.
}
} // namespace
core::Result<SurfaceModel> SurfaceModel::prepare(SurfaceInput input, const Surface& method,
                                                 core::Budget& budget,
                                                 const core::Cancellation& cancellation,
                                                 IlluminationReport& report) {
    report = {.status = SurfaceStatus::failed, .requested = method.parameters()};
    if (cancellation.requested(core::Checkpoint::measurement)) {
        return core::cancelled();
    }
    const FitContext context{
        .input = input,
        .method = method,
        .budget = budget,
        .cancellation = cancellation,
        .report = report,
    };
    auto counted = count_eligible(context);
    if (!counted) {
        return std::unexpected(counted.error());
    }
    SurfaceModel model{{.extent = input.source.get().extent()}, method, {}, 0, false};
    const auto proceed = should_fit(context);
    if (!proceed) {
        return std::unexpected(proceed.error());
    }
    if (!*proceed) {
        report.complete = true;
        return model;
    }
    auto grid = surface_grid(input.source.get().extent(), method);
    if (!grid) {
        return std::unexpected(grid.error());
    }
    model.grid_ = *grid;
    auto logs = fit_grid(*grid, context);
    if (!logs) {
        return std::unexpected(logs.error());
    }
    if (logs->empty()) {
        report.complete = true;
        return model;
    }
    model.logarithms_ = std::move(*logs);
    auto samples = measure_surface(input, model, budget, cancellation);
    if (!samples) {
        return std::unexpected(samples.error());
    }
    samples->target = method.parameters().target.value_or(samples->target);
    report.measurements = *samples;
    model.target_ = samples->target;
    const bool eligible = surface_eligible(report);
    if (method.parameters().mode == SurfaceMode::automatic && !eligible) {
        report.status = SurfaceStatus::skipped;
        report.reason = SurfaceReason::automatic_predicates;
        report.complete = true;
        return model;
    }
    model.active_ = true;
    report.status = SurfaceStatus::no_change;
    report.reason = SurfaceReason::no_effect;
    return model;
}
} // namespace docenhance::methods
