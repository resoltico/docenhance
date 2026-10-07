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
#include <optional>
#include <span>
#include <type_traits>
#include <variant>
namespace docenhance::methods {
core::Result<image::Rgb> ContrastModel::map_pixel(const image::Rgb& before,
                                                  image::RowRange position,
                                                  ContrastReport& report) const {
    const auto& method = method_;
    const auto& range = levels_;
    auto y = image::luminance(before);
    if (!y) {
        return std::unexpected(y.error());
    }
    auto f = image::srgb_encode(*y);
    if (!f) {
        return std::unexpected(f.error());
    }
    const auto* const levels = std::get_if<Levels>(&method);
    if ((levels != nullptr) != range.has_value()) {
        return core::failure(core::ErrorCode::invariant, "Levels mapping has no measured range");
    }
    const auto* const clahe = std::get_if<Clahe>(&method);
    auto candidate = [&] -> core::Result<double> {
        if (range) {
            return levels_candidate(*f, *range);
        }
        if (clahe != nullptr) {
            return clahe_candidate(*f, position, extent_, *clahe, maps_);
        }
        return gamma_candidate(*f, std::get<Gamma>(method));
    }();
    if (!candidate) {
        return std::unexpected(candidate.error());
    }
    const auto blend = std::visit(
        [](const auto& selected) {
            using M = std::decay_t<decltype(selected)>;
            if constexpr (std::is_same_v<M, ContrastOff>) {
                return 0.0;
            } else {
                return selected.parameters().blend;
            }
        },
        method);
    auto next = image::blend_perceptual(before, *f, *candidate, blend);
    if (!next) {
        return std::unexpected(next.error());
    }
    ++report.evaluated_samples;
    report.corrected_samples += static_cast<std::uint64_t>(*candidate != *f);
    report.changed_samples += static_cast<std::uint64_t>(*next != before);
    if (range) {
        report.clipped_low_samples += static_cast<std::uint64_t>(*f < range->low);
        report.clipped_high_samples += static_cast<std::uint64_t>(*f > range->high);
    }
    return *next;
}
core::Result<void> ContrastModel::apply(image::RowRange range, std::span<double> rgb,
                                        image::PlaneView<const std::uint8_t> mask,
                                        ContrastReport& report,
                                        const core::Cancellation& cancellation) const {
    if (!active_ || range.row >= extent_.height || range.first >= extent_.width || rgb.empty() ||
        rgb.size() % image::rgb_channels != 0 ||
        rgb.size() / image::rgb_channels > image::linear_block_pixels ||
        rgb.size() / image::rgb_channels > extent_.width - range.first ||
        (!mask.empty() && (mask.width() != extent_.width || mask.height() != extent_.height))) {
        return core::failure(core::ErrorCode::argument, "Invalid contrast reconstruction block");
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
