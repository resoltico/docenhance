// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/otsu.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace docenhance::tests {
inline std::uint16_t otsu_reference_bin(std::uint8_t sample) {
    constexpr double scale = 4095.0 / 255.0;
    return static_cast<std::uint16_t>(std::floor((static_cast<double>(sample) * scale) + 0.5));
}
// Enumerate candidate populations afresh from byte frequencies. No production prefix recurrence,
// production quantizer or histogram scratch is used by this independent finite-domain reference.
inline methods::OtsuObservation otsu_reference(image::PlaneView<const std::uint8_t> source) {
    std::array<std::uint64_t, 256> counts{};
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        for (const auto sample : source.row(y)) {
            ++counts.at(sample);
        }
    }
    const auto occupied = std::ranges::count_if(counts, [](auto count) { return count != 0; });
    if (occupied == 1) {
        return {.threshold_bin = 2047, .single_bin_fallback = true};
    }
    std::array<double, 4095> scores{};
    scores.fill(-1.0);
    double best = 0;
    for (std::size_t threshold = 0; threshold < scores.size(); ++threshold) {
        std::array<std::uint64_t, 2> populations{};
        std::array<std::uint64_t, 2> moments{};
        for (std::size_t sample = 0; sample < counts.size(); ++sample) {
            const auto bin = otsu_reference_bin(static_cast<std::uint8_t>(sample));
            const std::size_t group = bin <= threshold ? 0U : 1U;
            populations.at(group) += counts.at(sample);
            moments.at(group) += bin * counts.at(sample);
        }
        if (populations.at(0) == 0 || populations.at(1) == 0) {
            continue;
        }
        const auto n0 = static_cast<double>(populations.at(0));
        const auto n1 = static_cast<double>(populations.at(1));
        const double total = n0 + n1;
        const double difference =
            (static_cast<double>(moments.at(0)) / n0) - (static_cast<double>(moments.at(1)) / n1);
        scores.at(threshold) = (n0 / total) * (n1 / total) * difference * difference;
        best = std::max(best, scores.at(threshold));
    }
    constexpr double relative_tolerance = 1e-12;
    const double tolerance = relative_tolerance * std::max(1.0, best);
    for (std::size_t threshold = 0; threshold < scores.size(); ++threshold) {
        if (scores.at(threshold) >= 0 && best - scores.at(threshold) <= tolerance) {
            return {.threshold_bin = static_cast<std::uint16_t>(threshold)};
        }
    }
    return {};
}
} // namespace docenhance::tests
