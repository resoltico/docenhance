// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "otsu_reference.hpp"

#include "docenhance/image/plane.hpp"
#include "docenhance/methods/otsu.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace docenhance::tests {
namespace {
std::uint16_t exhaustive_bin(std::uint8_t sample) {
    return static_cast<std::uint16_t>(std::round(4095.0 * (static_cast<double>(sample) / 255.0)));
}
// Retain the full-bin oracle: repartition all 256 byte frequencies at every candidate threshold.
// It does not use the optimized reference's candidate selection or class-statistics helper.
methods::OtsuObservation exhaustive_otsu(image::PlaneView<const std::uint8_t> source) {
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
            const auto bin = exhaustive_bin(static_cast<std::uint8_t>(sample));
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
void compare_otsu_references(std::span<const std::uint8_t> pixels) {
    const auto width = static_cast<std::uint32_t>(pixels.size());
    const auto source = image::PlaneView<const std::uint8_t>::create(
                            pixels, {.width = width, .height = 1, .stride = width})
                            .value();
    CHECK(otsu_reference(source) == exhaustive_otsu(source));
}
} // namespace
TEST_CASE("Occupied Otsu candidates retain every exhaustive histogram plateau",
          "[otsu][reference]") {
    const std::vector<std::vector<std::uint8_t>> fixtures{
        {0},
        {127},
        {128},
        {255},
        {0, 255},
        {85, 170},
        {0, 85, 170},
        {85, 170, 255},
        {0, 0, 1, 2, 3, 254, 255},
        {1, 1, 254, 254},
    };
    for (const auto& pixels : fixtures) {
        compare_otsu_references(pixels);
    }
    std::array<std::uint8_t, 256> all_bytes{};
    for (std::size_t sample = 0; sample < all_bytes.size(); ++sample) {
        all_bytes.at(sample) = static_cast<std::uint8_t>(sample);
        CHECK(otsu_reference_bin(all_bytes.at(sample)) == exhaustive_bin(all_bytes.at(sample)));
    }
    compare_otsu_references(all_bytes);
    for (unsigned seed = 1; seed <= 8; ++seed) {
        std::array<std::uint8_t, 257> pixels{};
        for (std::size_t x = 0; x < pixels.size(); ++x) {
            pixels.at(x) = static_cast<std::uint8_t>(((x * x * 29U) + (seed * x * 71U)) % 256U);
        }
        compare_otsu_references(pixels);
    }
}
TEST_CASE("Occupied Otsu candidates preserve the global relative tie threshold",
          "[otsu][reference]") {
    for (const auto count : {1000U, 10000U}) {
        std::vector<std::uint8_t> pixels(count, 0);
        pixels.push_back(85);
        pixels.insert(pixels.end(), count + 1, 170);
        compare_otsu_references(pixels);
        const auto width = static_cast<std::uint32_t>(pixels.size());
        const auto source = image::PlaneView<const std::uint8_t>::create(
                                pixels, {.width = width, .height = 1, .stride = width})
                                .value();
        CHECK(otsu_reference(source).threshold_bin == (count == 10000 ? 0 : 1365));
    }
}
} // namespace docenhance::tests
