// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/geometry.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
namespace docenhance::methods {
inline constexpr double morphology_low_quantile = 0.1;
inline constexpr double morphology_high_quantile = 0.9;
inline constexpr std::uint32_t illumination_sample_limit = 1048576;
inline constexpr std::uint64_t morphology_minimum_samples = 16;
inline constexpr std::uint32_t morphology_lattice_extent = 1024;
inline constexpr std::uint32_t morphology_automatic_min = 8;
inline constexpr std::uint32_t morphology_automatic_max = 128;
constexpr double morphology_sigma(std::uint32_t radius) noexcept {
    return static_cast<double>(radius) / 2;
}
constexpr std::uint32_t morphology_gaussian_radius(std::uint32_t radius) noexcept {
    constexpr std::uint32_t support_sigmas = 3;
    return ((support_sigmas * radius) + 1) / 2;
}
inline std::uint32_t morphology_lattice_stride(image::Extent extent) noexcept {
    const auto maximum = std::max(extent.width, extent.height);
    return std::max(1U, (maximum / morphology_lattice_extent) +
                            static_cast<std::uint32_t>(maximum % morphology_lattice_extent != 0));
}
[[nodiscard]] double morphology_gaussian_weights(std::span<double> weights, double sigma,
                                                 std::uint32_t gaussian_radius) noexcept;
struct MorphologySampling {
    std::uint32_t stride{};
    std::uint32_t count{};
    bool fallback = false;
};
} // namespace docenhance::methods
