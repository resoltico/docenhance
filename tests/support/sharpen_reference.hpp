// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>
namespace docenhance::tests {
struct SharpenReferenceSettings {
    std::uint32_t width;
    std::uint32_t height;
    double sigma;
    double amount;
    double threshold;
};
struct SharpenReferencePosition {
    std::uint32_t x;
    std::uint32_t y;
};
struct SharpenReferenceGaussian {
    std::int64_t radius;
    std::vector<double> weights;
    double normalization = 0;
};
// Independent perceptual-to-linear reference. IEC 61966-2-1's published
// branch constants do not form a mathematically exact round trip at the knot.
// Comparing encode(actual) with the undecoded target is therefore incorrect.
inline double sharpen_reference_srgb_decode(double encoded) {
    return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
}
inline double sharpen_reference_srgb_encode(double linear) {
    return linear <= 0.0031308 ? 12.92 * linear : (1.055 * std::pow(linear, 1.0 / 2.4)) - 0.055;
}
inline double sharpen_reference_linear_output(double entering_linear, double entering_encoded,
                                              double candidate_encoded) {
    const auto candidate = std::clamp(candidate_encoded, 0.0, 1.0);
    // Production preserves the exact original sample for a perceptual no-op.
    return candidate == entering_encoded ? entering_linear
                                         : sharpen_reference_srgb_decode(candidate);
}
inline bool sharpen_reference_gray_rgb_matches(std::span<const double> actual,
                                               double expected_linear, double tolerance = 2e-12) {
    return actual.size() == 3 && std::isfinite(expected_linear) &&
           std::ranges::all_of(actual, [expected_linear, tolerance](double value) {
               return std::isfinite(value) && std::abs(value - expected_linear) < tolerance;
           });
}
inline std::uint32_t sharpen_reflect(std::int64_t index, std::uint32_t size) {
    if (size == 1) {
        return 0;
    }
    while (index < 0 || std::cmp_greater_equal(index, size)) {
        index = index < 0 ? -index : ((2 * std::int64_t{size}) - index) - 2;
    }
    return static_cast<std::uint32_t>(index);
}
inline SharpenReferenceGaussian sharpen_reference_gaussian(double sigma) {
    SharpenReferenceGaussian gaussian{
        .radius = static_cast<std::int64_t>(std::ceil(3 * sigma)),
        .weights = {},
    };
    for (std::int64_t j = -gaussian.radius; j <= gaussian.radius; ++j) {
        for (std::int64_t i = -gaussian.radius; i <= gaussian.radius; ++i) {
            const auto weight =
                std::exp(-static_cast<double>((i * i) + (j * j)) / (2 * sigma * sigma));
            gaussian.weights.push_back(weight);
            gaussian.normalization += weight;
        }
    }
    return gaussian;
}
// Direct two-dimensional convolution, independent of production separable passes.
inline double sharpen_reference_blur(const std::vector<double>& values,
                                     const SharpenReferenceSettings& settings,
                                     SharpenReferencePosition position,
                                     const SharpenReferenceGaussian& gaussian) {
    double total = 0;
    std::size_t tap = 0;
    for (std::int64_t j = -gaussian.radius; j <= gaussian.radius; ++j) {
        for (std::int64_t i = -gaussian.radius; i <= gaussian.radius; ++i) {
            const auto x = sharpen_reflect(std::int64_t{position.x} + i, settings.width);
            const auto y = sharpen_reflect(std::int64_t{position.y} + j, settings.height);
            total += gaussian.weights.at(tap++) * values.at((std::size_t{y} * settings.width) + x);
        }
    }
    return total / gaussian.normalization;
}
inline std::vector<double> sharpen_reference(const std::vector<double>& values,
                                             const SharpenReferenceSettings& settings) {
    const auto gaussian = sharpen_reference_gaussian(settings.sigma);
    std::vector<double> output;
    for (std::uint32_t y = 0; y < settings.height; ++y) {
        for (std::uint32_t x = 0; x < settings.width; ++x) {
            const auto value = values.at((std::size_t{y} * settings.width) + x);
            const auto d =
                value - sharpen_reference_blur(values, settings, {.x = x, .y = y}, gaussian);
            constexpr double maximum = 255;
            const auto cut = settings.threshold / maximum;
            output.push_back(
                value + (std::abs(d) <= cut ? 0 : settings.amount * (d - (d > 0 ? cut : -cut))));
        }
    }
    return output;
}
} // namespace docenhance::tests
