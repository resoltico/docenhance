// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace docenhance::methods {
namespace {
std::uint32_t divided_up(std::uint32_t extent, std::uint32_t cell) noexcept {
    return (extent / cell) + static_cast<std::uint32_t>(extent % cell != 0);
}
} // namespace
core::Result<SurfaceGrid> surface_grid(image::Extent extent, const Surface& method) {
    if (extent.width == 0 || extent.height == 0) {
        return core::failure(core::ErrorCode::argument, "I01 requires nonempty source extents");
    }
    constexpr std::uint32_t auto_min = 16;
    constexpr std::uint32_t auto_max = 256;
    constexpr double divisions = 24;
    const auto automatic =
        std::clamp(static_cast<std::uint32_t>(
                       std::floor((std::min(extent.width, extent.height) / divisions) + 0.5)),
                   auto_min, auto_max);
    const auto cell = method.parameters().cell.value_or(automatic);
    const auto columns = divided_up(extent.width, cell);
    const auto rows = divided_up(extent.height, cell);
    if (std::uint64_t{columns} * rows > surface_cell_limit) {
        return core::failure(core::ErrorCode::resource, "I01 exceeds the 65536-cell grid limit");
    }
    return SurfaceGrid{.extent = extent, .cell = cell, .columns = columns, .rows = rows};
}
bool protected_at(image::PlaneView<const std::uint8_t> mask, std::uint32_t x,
                  std::uint32_t y) noexcept {
    return !mask.empty() && mask.row(y).subspan(x, 1).front() != 0;
}
double select_quantile(std::span<double> samples, double quantile) {
    const auto rank =
        static_cast<std::size_t>(std::ceil(quantile * static_cast<double>(samples.size())));
    const auto index = rank == 0 ? 0 : std::min(rank - 1, samples.size() - 1);
    std::ranges::nth_element(samples, samples.begin() + static_cast<std::ptrdiff_t>(index));
    return surface_at(samples, index);
}
} // namespace docenhance::methods
