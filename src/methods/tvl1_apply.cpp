// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
namespace docenhance::methods {
core::Result<image::Rgb> tvl1_correct(const image::Rgb& rgb, double input, double output,
                                      double blend) {
    return image::blend_perceptual(rgb, input, output, blend);
}
core::Result<void> Tvl1Model::apply(image::RowRange range, std::span<double> rgb,
                                    image::PlaneView<const std::uint8_t> protection,
                                    DenoisingReport& report,
                                    const core::Cancellation& cancellation) const {
    if (!active() || range.row >= input_.height() || range.first >= input_.width() || rgb.empty() ||
        rgb.size() % image::rgb_channels != 0 ||
        rgb.size() / image::rgb_channels > image::linear_block_pixels ||
        rgb.size() / image::rgb_channels > input_.width() - range.first ||
        (!protection.empty() &&
         (protection.width() != input_.width() || protection.height() != input_.height()))) {
        return core::failure(core::ErrorCode::argument, "Invalid TV-L1 reconstruction block");
    }
    constexpr std::size_t interval = 128;
    for (std::size_t i = 0; i < rgb.size() / image::rgb_channels; ++i) {
        if (i % interval == 0 && cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const auto x = range.first + static_cast<std::uint32_t>(i);
        if (!protection.empty() && protection.row(range.row).subspan(x, 1).front() != 0) {
            continue;
        }
        const auto pixel = rgb.subspan(i * image::rgb_channels, image::rgb_channels);
        const image::Rgb before{
            pixel.front(),
            pixel.subspan(1, 1).front(),
            pixel.subspan(2, 1).front(),
        };
        const auto f = input_.view().row(range.row).subspan(x, 1).front();
        const auto u = result_.view().row(range.row).subspan(x, 1).front();
        auto next = tvl1_correct(before, f, u, blend_);
        if (!next) {
            return std::unexpected(next.error());
        }
        ++report.evaluated_samples;
        report.corrected_samples += static_cast<std::uint64_t>(f != u);
        report.changed_samples += static_cast<std::uint64_t>(*next != before);
        std::ranges::copy(*next, pixel.begin());
    }
    return {};
}
} // namespace docenhance::methods
