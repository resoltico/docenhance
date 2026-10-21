// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "nlm_math.hpp"

#include "docenhance/image/raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
namespace docenhance::methods {
std::uint16_t nlm_code(double perceptual) noexcept {
    constexpr double half_sample = 0.5;
    return static_cast<std::uint16_t>(std::floor((image::word_max * perceptual) + half_sample));
}
double nlm_candidate(double perceptual, std::uint16_t code, double mean) noexcept {
    return std::clamp(perceptual + ((mean - static_cast<double>(code)) / image::word_max), 0.0,
                      1.0);
}
double nlm_weight(std::uint32_t distance, double normalization) noexcept {
    const double t = static_cast<double>(distance) / normalization;
    return std::exp(-(t * t));
}
} // namespace docenhance::methods
