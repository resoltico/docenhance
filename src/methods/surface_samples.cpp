// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>

namespace docenhance::methods {
namespace {
struct Samples {
    std::span<double> y;
    std::span<double> b;
    std::span<double> rgb;
    std::uint32_t count{};
    std::uint32_t paper{};
    std::uint32_t dark{};
};
struct Sampling {
    SurfaceInput input;
    std::reference_wrapper<const SurfaceModel> model;
    std::reference_wrapper<const core::Cancellation> cancellation;
};
core::Result<void> append_sample(Samples& samples, Sampling sampling, image::Coordinate p,
                                 std::span<const double> triplet) {
    const auto y =
        image::luminance({triplet.front(), surface_at(triplet, 1), surface_at(triplet, 2)});
    if (!y) {
        return std::unexpected(y.error());
    }
    const auto background = sampling.model.get().background(p.x, p.y);
    if (!background) {
        return std::unexpected(background.error());
    }
    constexpr double paper_ratio = 0.8;
    constexpr double dark_ratio = 0.75;
    surface_at(samples.y, samples.count) = *y;
    surface_at(samples.b, samples.count) = *background;
    ++samples.count;
    samples.paper += static_cast<std::uint32_t>(*y >= paper_ratio * *background);
    samples.dark += static_cast<std::uint32_t>(*y < dark_ratio * *background);
    return {};
}
core::Result<void> sample_row(Samples& samples, Sampling sampling, std::uint32_t row,
                              std::uint32_t stride) {
    const auto width = sampling.input.source.get().extent().width;
    for (std::uint32_t first = 0; first < width && samples.count < surface_sample_limit;) {
        if (sampling.cancellation.get().requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        const auto count = std::min(image::linear_block_pixels, width - first);
        auto const rgb = samples.rgb.first(std::size_t{count} * image::rgb_channels);
        const auto read = sampling.input.source.get().read({.row = row, .first = first}, rgb,
                                                           image::RowUse::measurement);
        if (!read) {
            return read;
        }
        for (std::uint32_t x = 0; x < count && samples.count < surface_sample_limit; ++x) {
            const auto column = first + x;
            if (column % stride != 0 || protected_at(sampling.input.protection, column, row)) {
                continue;
            }
            const auto added = append_sample(
                samples, sampling, {.x = column, .y = row},
                rgb.subspan(std::size_t{x} * image::rgb_channels, image::rgb_channels));
            if (!added) {
                return added;
            }
        }
        first += count;
    }
    return {};
}
core::Result<void> gather(Samples& samples, Sampling sampling, std::uint32_t stride) {
    const auto height = sampling.input.source.get().extent().height;
    for (std::uint64_t row = 0; row < height && samples.count < surface_sample_limit;
         row += stride) {
        auto result = sample_row(samples, sampling, static_cast<std::uint32_t>(row), stride);
        if (!result) {
            return result;
        }
    }
    return {};
}
} // namespace
core::Result<SurfaceMeasurements> measure_surface(SurfaceInput input, const SurfaceModel& model,
                                                  core::Budget& budget,
                                                  const core::Cancellation& cancellation) {
    const auto extent = input.source.get().extent();
    const auto capacity = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(std::uint64_t{extent.width} * extent.height, surface_sample_limit));
    auto values = image::Plane<double>::allocate(budget, capacity, 2);
    if (!values) {
        return std::unexpected(values.error());
    }
    auto block =
        image::Plane<double>::allocate(budget, image::linear_block_pixels * image::rgb_channels, 1);
    if (!block) {
        return std::unexpected(block.error());
    }
    Samples samples{
        .y = values->view().row(0),
        .b = values->view().row(1),
        .rgb = block->view().row(0),
    };
    constexpr std::uint32_t lattice_extent = 1024;
    const auto maximum =
        std::max(input.source.get().extent().width, input.source.get().extent().height);
    const auto stride =
        std::max(std::uint32_t{1}, (maximum / lattice_extent) +
                                       static_cast<std::uint32_t>(maximum % lattice_extent != 0));
    const Sampling sampling{.input = input, .model = model, .cancellation = cancellation};
    auto sampled = gather(samples, sampling, stride);
    if (!sampled) {
        return std::unexpected(sampled.error());
    }
    const bool fallback = samples.count == 0;
    if (fallback) {
        sampled = gather(samples, sampling, 1);
        if (!sampled) {
            return std::unexpected(sampled.error());
        }
    }
    if (samples.count == 0) {
        return core::failure(core::ErrorCode::method_inapplicable, "No eligible surface samples");
    }
    if (cancellation.requested(core::Checkpoint::measurement)) {
        return core::cancelled();
    }
    const auto b = samples.b.first(samples.count);
    const auto y = samples.y.first(samples.count);
    constexpr double low_quantile = 0.1;
    constexpr double high_quantile = 0.9;
    const double b10 = select_quantile(b, low_quantile);
    const double b50 = select_quantile(b, 0.5);
    const double b90 = select_quantile(b, high_quantile);
    const double y90 = select_quantile(y, high_quantile);
    return SurfaceMeasurements{
        .stride = stride,
        .count = samples.count,
        .fallback = fallback,
        .target = b90,
        .background_q10 = b10,
        .background_q50 = b50,
        .background_q90 = b90,
        .luminance_q90 = y90,
        .variation = (b90 - b10) / std::max(b90, surface_floor),
        .paper_fraction = static_cast<double>(samples.paper) / samples.count,
        .dark_fraction = static_cast<double>(samples.dark) / samples.count,
    };
}
bool surface_eligible(IlluminationReport& report) {
    if (!report.measurements || report.cells == 0) {
        return false;
    }
    const auto& m = *report.measurements;
    constexpr double minimum_coverage = 0.60;
    constexpr double minimum_brightness = 0.35;
    constexpr double minimum_background = 0.20;
    constexpr double minimum_variation = 0.08;
    constexpr double minimum_paper = 0.55;
    constexpr double minimum_dark = 0.001;
    constexpr double maximum_dark = 0.40;
    report.predicates = {
        static_cast<double>(report.measured_cells) / report.cells >= minimum_coverage,
        m.luminance_q90 >= minimum_brightness,
        m.background_q50 >= minimum_background,
        m.variation >= minimum_variation,
        m.paper_fraction >= minimum_paper,
        m.dark_fraction >= minimum_dark && m.dark_fraction <= maximum_dark,
    };
    return std::ranges::all_of(report.predicates,
                               [](const auto& value) { return value.value_or(false); });
}
} // namespace docenhance::methods
