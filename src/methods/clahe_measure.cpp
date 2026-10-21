// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "contrast_detail.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/contrast.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
namespace docenhance::methods {
std::uint32_t tile_index(std::uint32_t coordinate, std::uint32_t size, std::uint32_t divisions) {
    // Inverse of floor(i*size/divisions), including uneven tile boundaries.
    return static_cast<std::uint32_t>((((std::uint64_t{coordinate} + 1) * divisions) - 1) / size);
}
core::Result<bool> make_knots(std::span<double> knots, std::span<const double> statistics,
                              double clip) {
    const auto count = statistics.front();
    constexpr double minimum_range = 1.0 / 4096;
    if (count < clahe_minimum_samples ||
        statistics.subspan(2, 1).front() - statistics.subspan(1, 1).front() < minimum_range) {
        for (std::uint32_t k = 0; k <= clahe_bins; ++k) {
            knots.subspan(k, 1).front() = static_cast<double>(k) / clahe_bins;
        }
        return true;
    }
    const auto limit = std::max(1.0, std::floor(clip * count / clahe_bins));
    std::uint64_t excess = 0;
    for (auto& mass : knots.first(clahe_bins)) {
        excess += static_cast<std::uint64_t>(std::max(0.0, mass - limit));
        mass = std::min(mass, limit);
    }
    const auto quotient = excess / clahe_bins;
    const auto remainder = excess % clahe_bins;
    double cumulative = 0;
    for (std::uint32_t k = 0; k < clahe_bins; ++k) {
        auto& knot = knots.subspan(k, 1).front();
        const double mass = knot + static_cast<double>(quotient) + (k < remainder ? 1 : 0);
        knot = cumulative / count;
        cumulative += mass;
    }
    if (cumulative != count) {
        return core::failure(core::ErrorCode::invariant, "CLAHE histogram mass changed");
    }
    knots.back() = 1;
    return false;
}
} // namespace docenhance::methods
