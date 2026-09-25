// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

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
void observe(double gain, double raw_target, bool capped, IlluminationReport& report) noexcept {
    if (report.evaluated_samples == 0) {
        report.min_gain = gain;
        report.max_gain = gain;
    } else {
        report.min_gain = std::min(report.min_gain, gain);
        report.max_gain = std::max(report.max_gain, gain);
    }
    ++report.evaluated_samples;
    report.gain_capped_samples += static_cast<std::uint64_t>(capped);
    report.saturated_samples += static_cast<std::uint64_t>(raw_target > 1);
}
core::Result<void> modify(std::span<double> pixel, double background,
                          const SurfaceParameters& options, double target,
                          IlluminationReport& report) {
    const image::Rgb original{pixel.front(), surface_at(pixel, 1), surface_at(pixel, 2)};
    const auto y = image::luminance(original);
    if (!y) {
        return std::unexpected(y.error());
    }
    const double ratio = target / std::max(background, surface_floor);
    const double gain = std::clamp(ratio, 1.0, options.max_gain);
    const double raw_target = *y * std::pow(gain, options.strength);
    const double desired = std::clamp(raw_target, 0.0, 1.0);
    observe(gain, raw_target, ratio >= options.max_gain, report);
    if (desired == *y) {
        return {};
    }
    const auto changed = image::transport_luminance(original, desired);
    if (!changed) {
        return std::unexpected(changed.error());
    }
    if (*changed != original) {
        ++report.changed_samples;
        report.status = SurfaceStatus::applied;
        report.reason = SurfaceReason::none;
        std::ranges::copy(*changed, pixel.begin());
    }
    return {};
}
} // namespace
core::Result<double> SurfaceModel::background(std::uint32_t x, std::uint32_t y) const {
    if (x >= grid_.extent.width || y >= grid_.extent.height || logarithms_.empty()) {
        return core::failure(core::ErrorCode::argument, "Coordinate has no fitted log surface");
    }
    const auto horizontal =
        interval({.extent = grid_.extent.width, .cell = grid_.cell, .count = grid_.columns}, x);
    const auto vertical =
        interval({.extent = grid_.extent.height, .cell = grid_.cell, .count = grid_.rows}, y);
    const auto sample = [&](std::uint32_t column, std::uint32_t row) {
        return surface_at(logarithms_.view().row(0), (std::size_t{row} * grid_.columns) + column);
    };
    const double top = std::lerp(sample(horizontal.first, vertical.first),
                                 sample(horizontal.second, vertical.first), horizontal.fraction);
    const double bottom =
        std::lerp(sample(horizontal.first, vertical.second),
                  sample(horizontal.second, vertical.second), horizontal.fraction);
    const double value = std::exp(std::lerp(top, bottom, vertical.fraction));
    if (!std::isfinite(value) || value <= 0) {
        return core::failure(core::ErrorCode::numerical, "Invalid interpolated log surface");
    }
    return value;
}
core::Result<void> SurfaceModel::apply(image::RowRange range, std::span<double> rgb,
                                       image::PlaneView<const std::uint8_t> protection,
                                       IlluminationReport& report,
                                       const core::Cancellation& cancellation) const {
    const auto extent = grid_.extent;
    if (range.row >= extent.height || range.first >= extent.width || rgb.empty() ||
        rgb.size() % image::rgb_channels != 0 ||
        rgb.size() / image::rgb_channels > image::linear_block_pixels ||
        rgb.size() / image::rgb_channels > extent.width - range.first ||
        (!protection.empty() &&
         (protection.width() != extent.width || protection.height() != extent.height))) {
        return core::failure(core::ErrorCode::argument, "Invalid surface application block");
    }
    if (cancellation.requested(core::Checkpoint::processing)) {
        return core::cancelled();
    }
    if (!active_) {
        return {};
    }
    for (std::size_t i = 0; i < rgb.size() / image::rgb_channels; ++i) {
        if (i % surface_poll_interval == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const auto x = range.first + static_cast<std::uint32_t>(i);
        if (protected_at(protection, x, range.row)) {
            continue;
        }
        const auto b = background(x, range.row);
        if (!b) {
            return std::unexpected(b.error());
        }
        const auto changed = modify(rgb.subspan(i * image::rgb_channels, image::rgb_channels), *b,
                                    method_.parameters(), target_, report);
        if (!changed) {
            return changed;
        }
    }
    return {};
}
} // namespace docenhance::methods
