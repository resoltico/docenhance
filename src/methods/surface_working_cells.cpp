// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "docenhance/methods/working.hpp"
#include "surface_detail.hpp"
#include "surface_working_detail.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::methods {
namespace {
core::Result<std::size_t> gather_cell(Working working, SurfaceCell cell,
                                      std::span<double> samples) {
    std::size_t count = 0;
    for (auto y = cell.y; y < cell.y + cell.height; ++y) {
        if (working.scheduler.get().cancellation().requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        const auto row = working.luminance.row(y);
        const auto mask = working.protection.empty() ? std::span<const std::uint8_t>{}
                                                     : working.protection.row(y);
        for (auto x = cell.x; x < cell.x + cell.width; ++x) {
            if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
                continue;
            }
            samples.subspan(count++, 1).front() = row.subspan(x, 1).front();
        }
    }
    return count;
}
} // namespace
core::Result<void> measure_surface_cells(Working working, const Surface& method,
                                         const SurfaceGrid& grid, image::PlaneView<double> work) {
    const auto capacity =
        std::min(grid.cell, grid.extent.width) * std::min(grid.cell, grid.extent.height);
    const auto tasks = surface_bands(grid.columns * grid.rows);
    auto samples = surface_allocate<double>(working, capacity, static_cast<std::uint32_t>(tasks));
    if (!samples) {
        return std::unexpected(std::move(samples.error()));
    }
    const auto task = [&](std::size_t band) -> core::Result<void> {
        const auto [first, last] = surface_band(grid.columns * grid.rows, band);
        const auto scratch = samples->view().row(static_cast<std::uint32_t>(band));
        for (auto index = first; index < last; ++index) {
            const auto cell = surface_cell(grid, index);
            auto count = gather_cell(working, cell, scratch);
            if (!count) {
                return std::unexpected(std::move(count.error()));
            }
            std::array<double, 2> values{};
            if (auto measured =
                    resolve_surface_cell(scratch.first(*count), cell.width * cell.height, method,
                                         values, working.scheduler.get().cancellation());
                !measured) {
                return measured;
            }
            work.row(0).subspan(index, 1).front() = values.front();
            work.row(1).subspan(index, 1).front() = values.back();
        }
        return {};
    };
    return working.scheduler.get().for_each(tasks, exec::WorkRef{task});
}
} // namespace docenhance::methods
