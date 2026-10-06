// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
namespace docenhance::methods {
core::Result<void> MorphologyModel::apply(image::RowRange range, std::span<double> rgb,
                                          image::PlaneView<const std::uint8_t> protection,
                                          IlluminationReport& report,
                                          const core::Cancellation& cancellation) const {
    if (range.row >= extent_.height || range.first >= extent_.width || rgb.empty() ||
        rgb.size() % image::rgb_channels != 0 ||
        rgb.size() / image::rgb_channels > image::linear_block_pixels ||
        rgb.size() / image::rgb_channels > extent_.width - range.first ||
        (!protection.empty() &&
         (protection.width() != extent_.width || protection.height() != extent_.height))) {
        return core::failure(core::ErrorCode::argument, "Invalid I02 application block");
    }
    if (cancellation.requested(core::Checkpoint::processing)) {
        return core::cancelled();
    }
    if (!active_) {
        return {};
    }
    constexpr std::size_t interval = 128;
    const auto p = method_.parameters();
    for (std::size_t i = 0; i < rgb.size() / image::rgb_channels; ++i) {
        if (i % interval == 0 && cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const auto x = range.first + static_cast<std::uint32_t>(i);
        if (!protection.empty() && protection.row(range.row).subspan(x, 1).front() != 0) {
            continue;
        }
        const auto value = background(x, range.row);
        if (!value) {
            return std::unexpected(value.error());
        }
        auto applied = apply_illumination_pixel(
            rgb.subspan(i * image::rgb_channels, image::rgb_channels), *value,
            {.target = target_, .strength = p.strength, .max_gain = p.max_gain}, report);
        if (!applied) {
            return applied;
        }
    }
    return {};
}
} // namespace docenhance::methods
