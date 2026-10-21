// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace docenhance::methods {
namespace {
struct Axis {
    std::uint32_t extent{};
    std::uint32_t cell{};
    std::uint32_t count{};
    [[nodiscard]] double center(std::uint32_t i) const noexcept {
        const auto first = std::uint64_t{i} * cell;
        return static_cast<double>(first) +
               ((static_cast<double>(std::min<std::uint64_t>(cell, extent - first)) - 1) / 2);
    }
};
struct Interval {
    std::uint32_t first{};
    std::uint32_t second{};
    double fraction{};
};
Interval interval(Axis axis, std::uint32_t coordinate) noexcept {
    std::uint32_t low = 0;
    std::uint32_t high = axis.count;
    while (low < high) {
        const auto middle = low + ((high - low) / 2);
        if (axis.center(middle) <= coordinate) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low == 0) {
        return {};
    }
    if (low == axis.count) {
        return {.first = low - 1, .second = low - 1};
    }
    return {
        .first = low - 1,
        .second = low,
        .fraction = (coordinate - axis.center(low - 1)) / (axis.center(low) - axis.center(low - 1)),
    };
}
} // namespace
double surface_background_value(const SurfaceGrid& grid, image::PlaneView<const double> logarithms,
                                image::Coordinate coordinate) noexcept {
    const auto horizontal = interval(
        {.extent = grid.extent.width, .cell = grid.cell, .count = grid.columns}, coordinate.x);
    const auto vertical = interval(
        {.extent = grid.extent.height, .cell = grid.cell, .count = grid.rows}, coordinate.y);
    const auto sample = [&](std::uint32_t column, std::uint32_t row) {
        return surface_at(logarithms.row(0), (std::size_t{row} * grid.columns) + column);
    };
    const double top = std::lerp(sample(horizontal.first, vertical.first),
                                 sample(horizontal.second, vertical.first), horizontal.fraction);
    const double bottom =
        std::lerp(sample(horizontal.first, vertical.second),
                  sample(horizontal.second, vertical.second), horizontal.fraction);
    return std::exp(std::lerp(top, bottom, vertical.fraction));
}
} // namespace docenhance::methods
