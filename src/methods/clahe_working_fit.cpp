// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "contrast_detail.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/working.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

namespace docenhance::methods {
namespace {
struct ClaheHistograms {
    image::PlaneView<double> knots;
    image::PlaneView<double> statistics;
    ClaheParameters parameters;
};
core::Result<void> initialize(ClaheHistograms histograms, const core::Cancellation& cancellation) {
    for (std::uint32_t tile = 0; tile < histograms.knots.height(); ++tile) {
        if (cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        std::ranges::fill(histograms.knots.row(tile), 0);
        const auto statistics = histograms.statistics.row(tile);
        statistics.front() = 0;
        statistics.subspan(1, 1).front() = 1;
        statistics.subspan(2, 1).front() = 0;
    }
    return {};
}
core::Result<void> gather_row(Working working, std::uint32_t y, ClaheHistograms histograms,
                              std::uint64_t& measured) {
    const auto row = working.luminance.row(y);
    const auto mask =
        working.protection.empty() ? std::span<const std::uint8_t>{} : working.protection.row(y);
    const auto cancellation = working.scheduler.get().cancellation();
    const auto p = histograms.parameters;
    const auto tile_row = tile_index(y, working.luminance.height(), p.grid_rows);
    for (std::size_t x = 0; x < row.size(); ++x) {
        constexpr std::size_t interval = 1024;
        if (x % interval == 0 && cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
            continue;
        }
        const auto f = image::srgb_encode(row.subspan(x, 1).front());
        const auto tile =
            (tile_row * p.grid_columns) +
            tile_index(static_cast<std::uint32_t>(x), working.luminance.width(), p.grid_columns);
        const auto bin = std::min(clahe_bins - 1, static_cast<std::uint32_t>(f * clahe_bins));
        histograms.knots.row(tile).subspan(bin, 1).front() += 1;
        const auto statistics = histograms.statistics.row(tile);
        statistics.front() += 1;
        auto& low = statistics.subspan(1, 1).front();
        auto& high = statistics.subspan(2, 1).front();
        low = std::min(low, f);
        high = std::max(high, f);
        ++measured;
    }
    return {};
}
core::Result<void> gather(Working working, ClaheHistograms histograms, std::uint64_t eligible) {
    std::uint64_t measured = 0;
    const auto work = [&](std::size_t) -> core::Result<void> {
        for (std::uint32_t y = 0; y < working.luminance.height(); ++y) {
            if (auto row = gather_row(working, y, histograms, measured); !row) {
                return row;
            }
        }
        return {};
    };
    if (auto result = working.scheduler.get().for_each(1, exec::WorkRef{work}); !result) {
        return result;
    }
    if (measured != eligible) {
        return core::failure(core::ErrorCode::invariant, "CLAHE eligibility changed");
    }
    return {};
}
core::Result<image::Plane<double>> allocate(std::uint32_t width, std::uint32_t height,
                                            Working working) {
    if (working.scheduler.get().cancellation().requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    return image::Plane<double>::allocate(working.budget.get(), width, height);
}
} // namespace
core::Result<ClaheMaps> fit_clahe(const Clahe& method, Working working, std::uint64_t eligible) {
    const auto p = method.parameters();
    const auto tiles = p.grid_columns * p.grid_rows;
    auto knots = allocate(clahe_bins + 1, tiles, working);
    if (!knots) {
        return std::unexpected(std::move(knots.error()));
    }
    auto statistics = allocate(clahe_statistic_channels, tiles, working);
    if (!statistics) {
        return std::unexpected(std::move(statistics.error()));
    }
    const ClaheHistograms histograms{
        .knots = knots->view(),
        .statistics = statistics->view(),
        .parameters = p,
    };
    const auto cancellation = working.scheduler.get().cancellation();
    if (auto initialized = initialize(histograms, cancellation); !initialized) {
        return std::unexpected(std::move(initialized.error()));
    }
    if (auto measured = gather(working, histograms, eligible); !measured) {
        return std::unexpected(std::move(measured.error()));
    }
    ClaheMaps maps{.knots = std::move(*knots)};
    for (std::uint32_t tile = 0; tile < tiles; ++tile) {
        if (cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        auto identity =
            make_knots(maps.knots.view().row(tile), statistics->view().row(tile), p.clip);
        if (!identity) {
            return std::unexpected(std::move(identity.error()));
        }
        maps.identity.at(tile) = *identity;
    }
    return maps;
}
} // namespace docenhance::methods
