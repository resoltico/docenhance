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
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
namespace docenhance::methods {
namespace {
core::Result<image::Plane<double>> allocate(const ContrastExecution& e, std::uint32_t width) {
    if (e.cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto plane = image::Plane<double>::allocate(e.budget.get(), width, 1);
    if (plane) {
        e.report.get().preparation_charge_peak =
            std::max(e.report.get().preparation_charge_peak,
                     static_cast<std::uint64_t>(e.budget.get().used()));
    }
    return plane;
}
core::Result<void> append(image::RowRange range, std::span<const double> rgb,
                          image::PlaneView<const std::uint8_t> mask, std::span<double> samples,
                          ContrastReport& r) {
    for (std::uint32_t i = 0; i < rgb.size() / image::rgb_channels; ++i) {
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
        if (!mask.empty() && mask.row(range.row).subspan(range.first + i, 1).front() != 0) {
            continue;
        }
        if (r.measured_samples >= samples.size()) {
            return core::failure(core::ErrorCode::invariant, "Contrast eligibility changed");
        }
        samples.subspan(static_cast<std::size_t>(r.measured_samples++), 1).front() = *f;
    }
    return {};
}
} // namespace
core::Result<LevelsRange> measure_levels(image::LinearSource& source,
                                         image::PlaneView<const std::uint8_t> mask,
                                         const Levels& method, const ContrastExecution& e) {
    auto samples = allocate(e, static_cast<std::uint32_t>(e.report.get().eligible_samples));
    if (!samples) {
        return std::unexpected(samples.error());
    }
    auto transfer = allocate(e, image::linear_block_pixels * image::rgb_channels);
    if (!transfer) {
        return std::unexpected(transfer.error());
    }
    const auto values = samples->view().row(0);
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        for (std::uint32_t first = 0; first < source.extent().width;) {
            if (e.cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            const auto n = std::min(image::linear_block_pixels, source.extent().width - first);
            const auto rgb = transfer->view().row(0).first(std::size_t{n} * image::rgb_channels);
            auto read = source.read({.row = y, .first = first}, rgb, e.preparation_use);
            if (!read) {
                return std::unexpected(read.error());
            }
            auto filled = append({.row = y, .first = first}, rgb, mask, values, e.report.get());
            if (!filled) {
                return std::unexpected(filled.error());
            }
            first += n;
        }
    }
    if (e.report.get().measured_samples != values.size()) {
        return core::failure(core::ErrorCode::invariant, "Incomplete contrast measurements");
    }
    auto sorted = image::sort_samples(values, e.cancellation);
    if (!sorted) {
        return std::unexpected(sorted.error());
    }
    const auto p = method.parameters();
    constexpr double percent_scale = Levels::maximum_high;
    auto lo = image::nearest_rank_index(values.size(), p.low / percent_scale);
    auto hi = image::nearest_rank_index(values.size(), p.high / percent_scale);
    if (!lo) {
        return std::unexpected(lo.error());
    }
    if (!hi) {
        return std::unexpected(hi.error());
    }
    return LevelsRange{
        .low = values.subspan(*lo, 1).front(),
        .high = values.subspan(*hi, 1).front(),
    };
}
} // namespace docenhance::methods
