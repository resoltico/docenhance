// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/surface_working.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/surface.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "surface_detail.hpp"
#include "surface_working_detail.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <utility>
namespace docenhance::methods {
namespace {
core::Result<SurfaceFacts> completed(SurfaceFacts facts, const Surface& method,
                                     image::Extent extent) {
    if (!validate(facts, method.parameters(), extent)) {
        return core::failure(core::ErrorCode::invariant, "I01 produced invalid completed facts");
    }
    return facts;
}
core::Result<SurfaceFacts> execute(const Surface& method, Working working, SurfaceFacts facts,
                                   const SurfaceGrid& grid) {
    auto fit = fit_surface(method, working, facts, grid);
    if (!fit) {
        return std::unexpected(std::move(fit.error()));
    }
    if (fit->logarithms.empty()) {
        return completed(fit->facts, method, fit->grid.extent);
    }
    if (!fit->facts.coverage) {
        return core::failure(core::ErrorCode::invariant, "Fitted surface lacks coverage");
    }
    auto measured = measure_surface_working(working, fit->grid, fit->logarithms.view().as_const(),
                                            fit->facts.counts.eligible);
    if (!measured) {
        return std::unexpected(std::move(measured.error()));
    }
    measured->target = method.parameters().target.value_or(measured->target);
    fit->facts.measurements = *measured;
    fit->facts.auto_predicates = surface_decisions(*measured, *fit->facts.coverage);
    const bool eligible =
        std::ranges::all_of(fit->facts.auto_predicates.values(),
                            [](const auto& value) { return value.value_or(false); });
    if (method.parameters().mode == SurfaceMode::automatic && !eligible) {
        fit->facts.disposition = Disposition::skipped;
        fit->facts.reason = SurfaceReason::automatic_predicates;
        return completed(fit->facts, method, fit->grid.extent);
    }
    if (auto applied = map_surface_working(working, method, fit->grid,
                                           fit->logarithms.view().as_const(), fit->facts);
        !applied) {
        return std::unexpected(std::move(applied.error()));
    }
    return completed(fit->facts, method, fit->grid.extent);
}
} // namespace
core::Result<SurfaceFacts> apply(const Surface& method, Working working) {
    auto counts = eligibility(working);
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    const auto p = method.parameters();
    const image::Extent extent{
        .width = working.luminance.width(),
        .height = working.luminance.height(),
    };
    SurfaceFacts facts{.counts = *counts};
    if (counts->eligible == 0) {
        return completed(facts, method, extent);
    }
    if (p.strength == 0) {
        facts.reason = SurfaceReason::zero_strength;
        return completed(facts, method, extent);
    }
    if (p.max_gain == 1) {
        facts.reason = SurfaceReason::unit_gain;
        return completed(facts, method, extent);
    }
    if (counts->eligible <
        std::min(surface_minimum_samples, std::uint64_t{extent.width} * extent.height)) {
        if (p.mode != SurfaceMode::automatic) {
            return core::failure(core::ErrorCode::method_inapplicable,
                                 "Insufficient eligible I01 measurements");
        }
        facts.disposition = Disposition::skipped;
        facts.reason = SurfaceReason::insufficient_samples;
        return completed(facts, method, extent);
    }
    auto grid = surface_grid(extent, method);
    if (!grid) {
        return std::unexpected(std::move(grid.error()));
    }
    return execute(method, working, facts, *grid);
}
} // namespace docenhance::methods
