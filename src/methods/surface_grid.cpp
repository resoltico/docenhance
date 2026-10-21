// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace docenhance::methods {
namespace {
struct CellAreaGroup {
    std::uint32_t area{};
    std::uint32_t count{};
};
std::uint32_t divided_up(std::uint32_t extent, std::uint32_t cell) noexcept {
    return (extent / cell) + static_cast<std::uint32_t>(extent % cell != 0);
}
} // namespace
std::uint32_t surface_cell_requirement(std::uint32_t area) noexcept {
    constexpr std::uint32_t minimum_samples = 16;
    constexpr std::uint32_t coverage_divisor = 4;
    return std::max(std::min(minimum_samples, area),
                    (area / coverage_divisor) +
                        static_cast<std::uint32_t>(area % coverage_divisor != 0));
}
SurfaceGrid surface_grid_shape(image::Extent extent, const Surface& method) noexcept {
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
    return SurfaceGrid{.extent = extent, .cell = cell, .columns = columns, .rows = rows};
}
core::Result<SurfaceGrid> surface_grid(image::Extent extent, const Surface& method) {
    if (extent.width == 0 || extent.height == 0) {
        return core::failure(core::ErrorCode::argument, "I01 requires nonempty source extents");
    }
    const auto grid = surface_grid_shape(extent, method);
    if (std::uint64_t{grid.columns} * grid.rows > surface_cell_limit) {
        return core::failure(core::ErrorCode::resource, "I01 exceeds the 65536-cell grid limit");
    }
    return grid;
}
std::uint64_t surface_minimum_support(const SurfaceGrid& grid,
                                      std::uint32_t positive_cells) noexcept {
    const auto last_width = grid.extent.width % grid.cell;
    const auto last_height = grid.extent.height % grid.cell;
    const auto partial_columns = static_cast<std::uint32_t>(last_width != 0);
    const auto partial_rows = static_cast<std::uint32_t>(last_height != 0);
    const auto full_columns = grid.columns - partial_columns;
    const auto full_rows = grid.rows - partial_rows;
    std::array groups{
        CellAreaGroup{.area = grid.cell * grid.cell, .count = full_columns * full_rows},
        CellAreaGroup{.area = last_width * grid.cell, .count = partial_columns * full_rows},
        CellAreaGroup{.area = grid.cell * last_height, .count = full_columns * partial_rows},
        CellAreaGroup{.area = last_width * last_height, .count = partial_columns * partial_rows},
    };
    std::ranges::sort(groups, {}, &CellAreaGroup::area);
    std::uint64_t support = 0;
    for (const auto& group : groups) {
        const auto selected = std::min(positive_cells, group.count);
        support += std::uint64_t{selected} * surface_cell_requirement(group.area);
        positive_cells -= selected;
    }
    return support;
}

bool protected_at(image::PlaneView<const std::uint8_t> mask, std::uint32_t x,
                  std::uint32_t y) noexcept {
    return !mask.empty() && mask.row(y).subspan(x, 1).front() != 0;
}
} // namespace docenhance::methods
