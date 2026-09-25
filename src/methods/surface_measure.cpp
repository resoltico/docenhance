// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace docenhance::methods {
namespace {
struct Cell {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
};
struct CellBuffers {
    std::span<double> samples;
    std::span<double> rgb;
};
core::Result<std::size_t> cell_samples(SurfaceInput input, Cell cell, CellBuffers buffers,
                                       const core::Cancellation& cancellation) {
    std::size_t count = 0;
    for (std::uint32_t row = cell.y; row < cell.y + cell.height; ++row) {
        if (cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        auto const rgb = buffers.rgb.first(std::size_t{cell.width} * image::rgb_channels);
        const auto read =
            input.source.get().read({.row = row, .first = cell.x}, rgb, image::RowUse::measurement);
        if (!read) {
            return std::unexpected(read.error());
        }
        for (std::uint32_t x = 0; x < cell.width; ++x) {
            if (protected_at(input.protection, cell.x + x, row)) {
                continue;
            }
            const auto triplet = rgb.subspan(std::size_t{x} * image::rgb_channels);
            const auto value =
                image::luminance({triplet.front(), surface_at(triplet, 1), surface_at(triplet, 2)});
            if (!value) {
                return std::unexpected(value.error());
            }
            surface_at(buffers.samples, count++) = *value;
        }
    }
    return count;
}
core::Result<void> measure_cell(SurfaceInput input, Cell cell, CellBuffers buffers,
                                std::span<double> output, MeasurementContext context) {
    const auto n = cell_samples(input, cell, buffers, context.cancellation.get());
    if (!n) {
        return std::unexpected(n.error());
    }
    constexpr std::uint32_t minimum_samples = 16;
    constexpr std::uint32_t coverage_divisor = 4;
    const auto area = cell.width * cell.height;
    const auto required = std::max(std::min(minimum_samples, area),
                                   (area / coverage_divisor) +
                                       static_cast<std::uint32_t>(area % coverage_divisor != 0));
    output.front() = 0;
    surface_at(output, 1) = 0;
    if (*n < required) {
        return {};
    }
    const double quantile =
        select_quantile(buffers.samples.first(*n), context.method.get().parameters().quantile);
    if (quantile >= surface_floor) {
        output.front() = static_cast<double>(*n) / area;
        surface_at(output, 1) = std::log(quantile);
    }
    return {};
}
} // namespace
core::Result<void> measure_cells(SurfaceInput input, const SurfaceGrid& grid,
                                 image::PlaneView<double> measurements,
                                 MeasurementContext context) {
    const auto sample_count =
        std::min(grid.cell, grid.extent.width) * std::min(grid.cell, grid.extent.height);
    auto samples = image::Plane<double>::allocate(context.budget.get(), sample_count, 1);
    if (!samples) {
        return std::unexpected(samples.error());
    }
    auto linear =
        image::Plane<double>::allocate(context.budget.get(), grid.cell * image::rgb_channels, 1);
    if (!linear) {
        return std::unexpected(linear.error());
    }
    const CellBuffers buffers{.samples = samples->view().row(0), .rgb = linear->view().row(0)};
    for (std::uint32_t j = 0; j < grid.columns * grid.rows; ++j) {
        const auto x = (j % grid.columns) * grid.cell;
        const auto y = (j / grid.columns) * grid.cell;
        const Cell cell{
            .x = x,
            .y = y,
            .width = std::min(grid.cell, grid.extent.width - x),
            .height = std::min(grid.cell, grid.extent.height - y),
        };
        std::array<double, 2> values{};
        const auto result = measure_cell(input, cell, buffers, values, context);
        if (!result) {
            return result;
        }
        surface_at(measurements.row(0), j) = values.at(0);
        surface_at(measurements.row(1), j) = values.at(1);
    }
    return {};
}
} // namespace docenhance::methods
