// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameter_domains.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
namespace docenhance::methods {
SurfaceCell surface_cell(const SurfaceGrid& grid, std::uint32_t index) noexcept {
    const auto x = (index % grid.columns) * grid.cell;
    const auto y = (index / grid.columns) * grid.cell;
    return {
        .x = x,
        .y = y,
        .width = std::min(grid.cell, grid.extent.width - x),
        .height = std::min(grid.cell, grid.extent.height - y),
    };
}
core::Result<void> resolve_surface_cell(std::span<double> samples, std::uint32_t area,
                                        const Surface& method, std::span<double> output,
                                        const core::Cancellation& cancellation) {
    const auto required = surface_cell_requirement(area);
    output.front() = 0;
    surface_at(output, 1) = 0;
    if (samples.size() < required) {
        return {};
    }
    const auto quantile = image::nearest_rank(samples, method.parameters().quantile, cancellation);
    if (!quantile) {
        return std::unexpected(quantile.error());
    }
    if (*quantile >= illumination_floor) {
        output.front() = static_cast<double>(samples.size()) / area;
        surface_at(output, 1) = std::log(*quantile);
    }
    return {};
}
core::Result<void> exclude_surface_dark_cells(image::PlaneView<double> work,
                                              std::optional<double>& background_reference,
                                              std::uint32_t& dark_cells,
                                              const core::Cancellation& cancellation) {
    const auto weights = work.row(0);
    const auto logs = work.row(1);
    const auto scratch = work.row(2);
    std::size_t measured = 0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (surface_at(weights, i) > 0) {
            surface_at(scratch, measured++) = surface_at(logs, i);
        }
    }
    if (measured == 0) {
        return {};
    }
    const auto selected =
        image::nearest_rank(scratch.first(measured), surface_reference_rank, cancellation);
    if (!selected) {
        return std::unexpected(selected.error());
    }
    const double reference = *selected;
    background_reference = std::exp(reference);
    const double floor = reference - std::log(background_max_gain_domain.maximum);
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (surface_at(weights, i) > 0 && surface_at(logs, i) < floor) {
            surface_at(weights, i) = 0;
            surface_at(logs, i) = 0;
            ++dark_cells;
        }
    }
    return {};
}
} // namespace docenhance::methods
