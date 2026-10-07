// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
namespace docenhance::methods {
core::Result<image::Rgb> SharpenModel::map_pixel(const image::Rgb& before, image::RowRange position,
                                                 SharpenReport& report) const {
    auto y = image::luminance(before);
    if (!y) {
        return std::unexpected(y.error());
    }
    auto f = image::srgb_encode(*y);
    if (!f) {
        return std::unexpected(f.error());
    }
    auto raw = unsharp_candidate(
        *f, blurred_.view().row(position.row).subspan(position.first, 1).front(), method_);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    auto next = image::blend_perceptual(before, *f, std::clamp(*raw, 0.0, 1.0), 1);
    if (!next) {
        return std::unexpected(next.error());
    }
    if (!report.pre_clamp) {
        report.pre_clamp = ExcursionRange{.low = *raw, .high = *raw};
    } else {
        report.pre_clamp->low = std::min(report.pre_clamp->low, *raw);
        report.pre_clamp->high = std::max(report.pre_clamp->high, *raw);
    }
    ++report.evaluated_samples;
    report.corrected_samples += static_cast<std::uint64_t>(*raw != *f);
    report.changed_samples += static_cast<std::uint64_t>(*next != before);
    report.clipped_low_samples += static_cast<std::uint64_t>(*raw < 0);
    report.clipped_high_samples += static_cast<std::uint64_t>(*raw > 1);
    return *next;
}
core::Result<void> SharpenModel::apply(image::RowRange range, std::span<double> rgb,
                                       image::PlaneView<const std::uint8_t> mask,
                                       SharpenReport& report,
                                       const core::Cancellation& cancellation) const {
    if (!active() || range.row >= extent_.height || range.first >= extent_.width || rgb.empty() ||
        rgb.size() % image::rgb_channels != 0 ||
        rgb.size() / image::rgb_channels > image::linear_block_pixels ||
        rgb.size() / image::rgb_channels > extent_.width - range.first ||
        (!mask.empty() && (mask.width() != extent_.width || mask.height() != extent_.height))) {
        return core::failure(core::ErrorCode::argument, "Invalid sharpening reconstruction block");
    }
    for (std::size_t i = 0; i < rgb.size() / image::rgb_channels; ++i) {
        constexpr std::size_t interval = 128;
        if (i % interval == 0 && cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const auto x = range.first + static_cast<std::uint32_t>(i);
        if (!mask.empty() && mask.row(range.row).subspan(x, 1).front() != 0) {
            continue;
        }
        const auto pixel = rgb.subspan(i * image::rgb_channels, image::rgb_channels);
        const image::Rgb before{
            pixel.front(),
            pixel.subspan(1, 1).front(),
            pixel.subspan(2, 1).front(),
        };
        auto next = map_pixel(before, {.row = range.row, .first = x}, report);
        if (!next) {
            return std::unexpected(next.error());
        }
        std::ranges::copy(*next, pixel.begin());
    }
    return {};
}
} // namespace docenhance::methods
