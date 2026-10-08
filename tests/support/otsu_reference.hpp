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
#include <span>

namespace docenhance::tests {
inline std::uint16_t otsu_reference_bin(std::uint8_t sample) {
    constexpr double scale = 4095.0 / 255.0;
    return static_cast<std::uint16_t>(std::floor((static_cast<double>(sample) * scale) + 0.5));
}
// Class statistics are recomputed independently for each candidate, without prefix recurrences.
inline double otsu_reference_score(std::span<const std::uint64_t> counts, std::uint16_t threshold) {
    std::array<std::uint64_t, 2> populations{};
    std::array<std::uint64_t, 2> moments{};
    for (std::size_t sample = 0; sample < counts.size(); ++sample) {
        const auto count = counts.subspan(sample, 1).front();
        if (count == 0) {
            continue;
        }
        const auto bin = otsu_reference_bin(static_cast<std::uint8_t>(sample));
        const std::size_t group = bin <= threshold ? 0U : 1U;
        populations.at(group) += count;
        moments.at(group) += bin * count;
    }
    if (populations.at(0) == 0 || populations.at(1) == 0) {
        return -1;
    }
    const auto n0 = static_cast<double>(populations.at(0));
    const auto n1 = static_cast<double>(populations.at(1));
    const double total = n0 + n1;
    const double difference =
        (static_cast<double>(moments.at(0)) / n0) - (static_cast<double>(moments.at(1)) / n1);
    return (n0 / total) * (n1 / total) * difference * difference;
}
// Scores are constant from each occupied bin until the next occupied bin. Evaluating their
// first thresholds retains all 4095 candidates' scores and earliest eligible threshold exactly.
// The byte-frequency class partition and floating quantizer remain independent of production.
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
    std::array<double, 256> scores{};
    scores.fill(-1.0);
    double best = 0;
    for (std::size_t sample = 0; sample < counts.size(); ++sample) {
        if (counts.at(sample) != 0) {
            scores.at(sample) =
                otsu_reference_score(counts, otsu_reference_bin(static_cast<std::uint8_t>(sample)));
            best = std::max(best, scores.at(sample));
        }
    }
    constexpr double relative_tolerance = 1e-12;
    const double tolerance = relative_tolerance * std::max(1.0, best);
    for (std::size_t sample = 0; sample < counts.size(); ++sample) {
        if (scores.at(sample) >= 0 && best - scores.at(sample) <= tolerance) {
            return {.threshold_bin = otsu_reference_bin(static_cast<std::uint8_t>(sample))};
        }
    }
    return {};
}
} // namespace docenhance::tests
