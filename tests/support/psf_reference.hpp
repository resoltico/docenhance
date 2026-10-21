// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <numeric>
#include <utility>
#include <vector>

namespace docenhance::tests {
inline std::vector<double> normalized_psf_reference(std::vector<long double> values) {
    const auto sum = std::accumulate(values.begin(), values.end(), 0.L);
    std::vector<double> result(values.size());
    std::ranges::transform(values, result.begin(),
                           [sum](long double value) { return static_cast<double>(value / sum); });
    return result;
}
inline std::vector<double> gaussian_psf_reference(double sigma) {
    // Independently factor the two-dimensional density; no production side/normalizer call.
    const auto radius = static_cast<int>(std::ceil(3 * sigma));
    const auto side = (2 * radius) + 1;
    std::vector<long double> values;
    values.reserve(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    const auto variance = 2.L * static_cast<long double>(sigma) * static_cast<long double>(sigma);
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            values.push_back(std::exp(-static_cast<long double>(x * x) / variance) *
                             std::exp(-static_cast<long double>(y * y) / variance));
        }
    }
    return normalized_psf_reference(std::move(values));
}
inline std::vector<double> motion_psf_reference(double length, double angle) {
    // Gather each destination's tent-basis exposure instead of scattering four deposits.
    const auto radius = static_cast<int>(std::ceil(length / 2)) + 1;
    const auto side = (2 * radius) + 1;
    const auto samples = std::max(64, static_cast<int>(std::ceil(32 * length)));
    const auto theta = static_cast<long double>(angle) * std::numbers::pi_v<long double> / 180;
    std::vector<long double> values;
    values.reserve(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            long double mass = 0;
            for (int n = 0; n < samples; ++n) {
                const auto t = (((static_cast<long double>(n) + .5L) / samples) - .5L) *
                               static_cast<long double>(length);
                mass += std::max(0.L, 1 - std::abs((t * std::cos(theta)) - x)) *
                        std::max(0.L, 1 - std::abs((t * std::sin(theta)) - y));
            }
            values.push_back(mass);
        }
    }
    return normalized_psf_reference(std::move(values));
}
} // namespace docenhance::tests
