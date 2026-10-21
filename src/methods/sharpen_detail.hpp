// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/image/gaussian.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/working.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace docenhance::methods {
inline constexpr std::size_t unsharp_coefficients_max = 19;
inline constexpr std::size_t unsharp_row_bands_max = 64;
[[nodiscard]] inline std::size_t unsharp_band_count(std::uint32_t height) noexcept {
    return std::min(unsharp_row_bands_max, static_cast<std::size_t>(height));
}
// Positive height and band < unsharp_band_count(height), independent of worker count.
[[nodiscard]] inline std::pair<std::uint32_t, std::uint32_t>
unsharp_rows(std::uint32_t height, std::size_t band) noexcept {
    const auto bands = unsharp_band_count(height);
    return {
        static_cast<std::uint32_t>(std::uint64_t{band} * height / bands),
        static_cast<std::uint32_t>(std::uint64_t{band + 1} * height / bands),
    };
}
// Borrows the caller's live coefficient storage. Each call supplies orientation/output rows.
[[nodiscard]] image::GaussianPass
unsharp_gaussian(std::span<double, unsharp_coefficients_max> storage,
                 const Unsharp& method) noexcept;
[[nodiscard]] core::Result<image::Plane<double>> fit_unsharp(const Unsharp& method,
                                                             Working working);
} // namespace docenhance::methods
