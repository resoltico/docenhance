// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/opencv/restoration.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
namespace docenhance::opencv {
core::Result<void> RestorationModel::apply(image::RowRange range, std::span<double> rgb,
                                           image::PlaneView<const std::uint8_t> mask,
                                           methods::RestorationReport& report,
                                           const core::Cancellation& cancellation) const {
    if (!active() || range.row >= extent_.height || range.first >= extent_.width || rgb.empty() ||
        rgb.size() % image::rgb_channels != 0 ||
        rgb.size() / image::rgb_channels > image::linear_block_pixels ||
        rgb.size() / image::rgb_channels > extent_.width - range.first ||
        (!mask.empty() && (mask.width() != extent_.width || mask.height() != extent_.height))) {
        return core::failure(core::ErrorCode::argument, "Invalid restoration reconstruction block");
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
        auto original = image::luminance(before);
        if (!original) {
            return std::unexpected(original.error());
        }
        const auto raw = candidate_.view().row(range.row).subspan(x, 1).front();
        auto target = methods::restoration_target(*original, raw, method_);
        if (!target) {
            return std::unexpected(target.error());
        }
        auto next = image::transport_luminance(before, std::clamp(*target, 0.0, 1.0));
        if (!next) {
            return std::unexpected(next.error());
        }
        ++report.evaluated_samples;
        report.corrected_samples += static_cast<std::uint64_t>(*target != *original);
        report.changed_samples += static_cast<std::uint64_t>(*next != before);
        report.raw_low_samples += static_cast<std::uint64_t>(raw < 0);
        report.raw_high_samples += static_cast<std::uint64_t>(raw > 1);
        report.blended_low_samples += static_cast<std::uint64_t>(*target < 0);
        report.blended_high_samples += static_cast<std::uint64_t>(*target > 1);
        std::ranges::copy(*next, pixel.begin());
    }
    return {};
}
} // namespace docenhance::opencv
