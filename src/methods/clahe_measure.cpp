// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "contrast_detail.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::methods {
namespace {
struct Histograms {
    image::PlaneView<double> knots;
    image::PlaneView<double> statistics;
    image::Extent extent;
    ClaheParameters parameters;
};
std::uint32_t tile_index(std::uint32_t coordinate, std::uint32_t size, std::uint32_t divisions) {
    // Inverse of floor(i*size/divisions), including uneven tile boundaries.
    return static_cast<std::uint32_t>((((std::uint64_t{coordinate} + 1) * divisions) - 1) / size);
}
core::Result<void> append(image::RowRange position, std::span<const double> rgb,
                          image::PlaneView<const std::uint8_t> mask, const Histograms& histograms,
                          const ContrastExecution& e) {
    const auto p = histograms.parameters;
    const auto tile_row = tile_index(position.row, histograms.extent.height, p.grid_rows);
    for (std::uint32_t i = 0; i < rgb.size() / image::rgb_channels; ++i) {
        constexpr std::uint32_t interval = 128;
        if (i % interval == 0 && e.cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        const auto pixel = rgb.subspan(std::size_t{i} * image::rgb_channels, image::rgb_channels);
        auto y = image::luminance(
            {pixel.front(), pixel.subspan(1, 1).front(), pixel.subspan(2, 1).front()});
        if (!y) {
            return std::unexpected(y.error());
        }
        auto f = image::srgb_encode(*y);
        if (!f) {
            return std::unexpected(f.error());
        }
        const auto x = position.first + i;
        if (!mask.empty() && mask.row(position.row).subspan(x, 1).front() != 0) {
            continue;
        }
        const auto tile =
            (tile_row * p.grid_columns) + tile_index(x, histograms.extent.width, p.grid_columns);
        const auto bin = std::min(clahe_bins - 1, static_cast<std::uint32_t>(*f * clahe_bins));
        histograms.knots.row(tile).subspan(bin, 1).front() += 1;
        const auto statistics = histograms.statistics.row(tile);
        statistics.front() += 1;
        auto& low = statistics.subspan(1, 1).front();
        auto& high = statistics.subspan(2, 1).front();
        low = std::min(low, *f);
        high = std::max(high, *f);
        ++e.report.get().measured_samples;
    }
    return {};
}
core::Result<void> measure(image::LinearSource& source, image::PlaneView<const std::uint8_t> mask,
                           const Histograms& histograms, std::span<double> transfer,
                           const ContrastExecution& e) {
    for (std::uint32_t tile = 0; tile < histograms.knots.height(); ++tile) {
        if (e.cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        std::ranges::fill(histograms.knots.row(tile), 0);
        const auto statistics = histograms.statistics.row(tile);
        statistics.front() = 0;
        statistics.subspan(1, 1).front() = 1;
        statistics.subspan(2, 1).front() = 0;
    }
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        for (std::uint32_t x = 0; x < source.extent().width;) {
            if (e.cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            const auto n = std::min(image::linear_block_pixels, source.extent().width - x);
            const auto rgb = transfer.first(std::size_t{n} * image::rgb_channels);
            auto read = source.read({.row = y, .first = x}, rgb, e.preparation_use);
            if (!read) {
                return std::unexpected(read.error());
            }
            auto measured = append({.row = y, .first = x}, rgb, mask, histograms, e);
            if (!measured) {
                return std::unexpected(measured.error());
            }
            x += n;
        }
    }
    return {};
}
core::Result<bool> make_knots(std::span<double> knots, std::span<const double> statistics,
                              double clip) {
    const auto count = statistics.front();
    constexpr double minimum_range = 1.0 / 4096;
    if (count < clahe_minimum_samples ||
        statistics.subspan(2, 1).front() - statistics.subspan(1, 1).front() < minimum_range) {
        for (std::uint32_t k = 0; k <= clahe_bins; ++k) {
            knots.subspan(k, 1).front() = static_cast<double>(k) / clahe_bins;
        }
        return true;
    }
    const auto limit = std::max(1.0, std::floor(clip * count / clahe_bins));
    std::uint64_t excess = 0;
    for (auto& mass : knots.first(clahe_bins)) {
        excess += static_cast<std::uint64_t>(std::max(0.0, mass - limit));
        mass = std::min(mass, limit);
    }
    const auto quotient = excess / clahe_bins;
    const auto remainder = excess % clahe_bins;
    double cumulative = 0;
    for (std::uint32_t k = 0; k < clahe_bins; ++k) {
        auto& knot = knots.subspan(k, 1).front();
        const double mass = knot + static_cast<double>(quotient) + (k < remainder ? 1 : 0);
        knot = cumulative / count;
        cumulative += mass;
    }
    if (cumulative != count) {
        return core::failure(core::ErrorCode::invariant, "CLAHE histogram mass changed");
    }
    knots.back() = 1;
    return false;
}
core::Result<image::Plane<double>> allocate(std::uint32_t width, std::uint32_t height,
                                            const ContrastExecution& e) {
    if (e.cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto plane = image::Plane<double>::allocate(e.budget.get(), width, height);
    if (plane) {
        e.report.get().preparation_charge_peak =
            std::max(e.report.get().preparation_charge_peak,
                     static_cast<std::uint64_t>(e.budget.get().used()));
    }
    return plane;
}
} // namespace
core::Result<ClaheMaps> measure_clahe(image::LinearSource& source,
                                      image::PlaneView<const std::uint8_t> mask,
                                      const Clahe& method, const ContrastExecution& e) {
    const auto p = method.parameters();
    const auto extent = source.extent();
    if (extent.width / p.grid_columns < clahe_minimum_tile ||
        extent.height / p.grid_rows < clahe_minimum_tile) {
        return core::failure(core::ErrorCode::argument,
                             "CLAHE tiles require at least 16 pixels per dimension");
    }
    const auto tiles = p.grid_columns * p.grid_rows;
    auto knots = allocate(clahe_bins + 1, tiles, e);
    if (!knots) {
        return std::unexpected(knots.error());
    }
    auto statistics = allocate(clahe_statistic_channels, tiles, e);
    if (!statistics) {
        return std::unexpected(statistics.error());
    }
    auto transfer = allocate(image::linear_block_pixels * image::rgb_channels, 1, e);
    if (!transfer) {
        return std::unexpected(transfer.error());
    }
    const Histograms histograms{
        .knots = knots->view(),
        .statistics = statistics->view(),
        .extent = extent,
        .parameters = p,
    };
    auto measured = measure(source, mask, histograms, transfer->view().row(0), e);
    if (!measured) {
        return std::unexpected(measured.error());
    }
    if (e.report.get().measured_samples != e.report.get().eligible_samples) {
        return core::failure(core::ErrorCode::invariant, "CLAHE eligibility changed");
    }
    ClaheMaps maps{.knots = std::move(*knots)};
    for (std::uint32_t tile = 0; tile < tiles; ++tile) {
        if (e.cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        auto identity =
            make_knots(maps.knots.view().row(tile), statistics->view().row(tile), p.clip);
        if (!identity) {
            return std::unexpected(identity.error());
        }
        maps.identity.at(tile) = *identity;
        e.report.get().identity_tiles += static_cast<std::uint32_t>(*identity);
    }
    return maps;
}
} // namespace docenhance::methods
