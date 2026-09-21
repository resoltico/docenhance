// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
namespace docenhance::image {
inline constexpr std::size_t rgb_channels = 3;
using Rgb = std::array<double, rgb_channels>;
// Scalar reference primitives, not complete color-management or enhancement methods.
[[nodiscard]] core::Result<double> srgb_decode(double encoded);
[[nodiscard]] core::Result<double> srgb_encode(double linear);
[[nodiscard]] core::Result<double> luminance(const Rgb& rgb);
[[nodiscard]] core::Result<Rgb> transport_luminance(const Rgb& rgb, double target);
[[nodiscard]] core::Result<double> nearest_rank(std::span<const double> values, double p);
[[nodiscard]] core::Result<std::size_t> reflect101(std::int64_t coordinate, std::size_t extent);
// The same fold without the validation, for a caller that has already validated the extent with
// reflect101 above. A kernel folds once per border sample and cannot afford to carry an Error.
[[nodiscard]] std::size_t reflect101_folded(std::int64_t coordinate, std::size_t extent) noexcept;
// The largest extent reflect101 can fold, so a caller can validate an extent once.
[[nodiscard]] constexpr std::size_t max_reflect_extent() noexcept {
    return static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max() / 2) + 1;
}
// Element count of a raster, rejecting zero extents and anything above the caller's budget.
[[nodiscard]] core::Result<std::size_t> checked_elements(std::size_t width, std::size_t height,
                                                         std::size_t channels, std::size_t limit);
} // namespace docenhance::image
