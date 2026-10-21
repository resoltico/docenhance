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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::methods {
namespace {
struct SurfaceSamples {
    std::span<double> y;
    std::span<double> b;
    SurfaceSampleCounts counts;
};
core::Result<void> sample_row(Working working, SurfaceFieldView field, SurfaceSamples& samples,
                              std::uint32_t y, std::uint32_t stride) {
    const auto row = working.luminance.row(y);
    const auto mask =
        working.protection.empty() ? std::span<const std::uint8_t>{} : working.protection.row(y);
    std::size_t visited = 0;
    for (std::uint32_t x = 0; x < row.size() && samples.counts.count < samples.y.size();
         x += stride) {
        if (visited++ % surface_poll_interval == 0 &&
            working.scheduler.get().cancellation().requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
            continue;
        }
        const auto background =
            surface_background_value(field.grid, field.logarithms, {.x = x, .y = y});
        if (!std::isfinite(background) || background <= 0) {
            return core::failure(core::ErrorCode::numerical, "Invalid interpolated log surface");
        }
        const auto value = row.subspan(x, 1).front();
        samples.y.subspan(samples.counts.count, 1).front() = value;
        samples.b.subspan(samples.counts.count++, 1).front() = background;
        samples.counts.paper += static_cast<std::uint32_t>(surface_paper_sample(value, background));
        samples.counts.dark += static_cast<std::uint32_t>(surface_dark_sample(value, background));
    }
    return {};
}
core::Result<void> gather(Working working, SurfaceFieldView field, SurfaceSamples& samples,
                          std::uint32_t stride) {
    for (std::uint32_t y = 0;
         y < working.luminance.height() && samples.counts.count < samples.y.size(); y += stride) {
        if (auto row = sample_row(working, field, samples, y, stride); !row) {
            return row;
        }
    }
    return {};
}
} // namespace
core::Result<SurfaceMeasurements> measure_surface_working(Working working, const SurfaceGrid& grid,
                                                          image::PlaneView<const double> logs,
                                                          std::uint64_t eligible) {
    const auto capacity =
        static_cast<std::uint32_t>(std::min(eligible, std::uint64_t{surface_sample_limit}));
    auto values = surface_allocate<double>(working, capacity, 2);
    if (!values) {
        return std::unexpected(std::move(values.error()));
    }
    SurfaceSamples samples{
        .y = values->view().row(0),
        .b = values->view().row(1),
        .counts = {.stride = surface_lattice_stride(grid.extent)},
    };
    const SurfaceFieldView field{.grid = grid, .logarithms = logs};
    const auto task = [&](std::size_t) -> core::Result<void> {
        if (auto sampled = gather(working, field, samples, samples.counts.stride); !sampled) {
            return sampled;
        }
        samples.counts.fallback = samples.counts.count == 0;
        if (samples.counts.fallback) {
            if (auto fallback = gather(working, field, samples, 1); !fallback) {
                return fallback;
            }
        }
        return {};
    };
    if (auto sampled = working.scheduler.get().for_each(1, exec::WorkRef{task}); !sampled) {
        return std::unexpected(std::move(sampled.error()));
    }
    if (samples.counts.count == 0) {
        return core::failure(core::ErrorCode::method_inapplicable, "No eligible surface samples");
    }
    if (working.scheduler.get().cancellation().requested(core::Checkpoint::measurement)) {
        return core::cancelled();
    }
    return surface_sample_measurements(samples.y.first(samples.counts.count),
                                       samples.b.first(samples.counts.count), samples.counts,
                                       working.scheduler.get().cancellation());
}
} // namespace docenhance::methods
