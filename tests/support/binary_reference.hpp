// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "require.hpp"
#include "sauvola_reference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace docenhance::tests {
inline constexpr std::size_t binary_reference_bins = 4096;
inline constexpr double binary_reference_max = 65535.0;
inline std::uint16_t binary_reference_bin(std::uint16_t code) {
    constexpr double last_bin = 4095.0;
    return static_cast<std::uint16_t>(
        std::floor(((static_cast<double>(code) * last_bin) / binary_reference_max) + 0.5));
}
struct BinaryOtsuReference {
    std::uint16_t threshold = 2047;
    bool fallback = false;
};
inline double binary_reference_score(const std::array<std::uint64_t, binary_reference_bins>& bins,
                                     std::size_t threshold) {
    std::array<std::uint64_t, 2> populations{};
    std::array<std::uint64_t, 2> moments{};
    for (std::size_t bin = 0; bin < bins.size(); ++bin) {
        const auto side = static_cast<std::size_t>(bin > threshold);
        populations.at(side) += bins.at(bin);
        moments.at(side) += bins.at(bin) * bin;
    }
    if (populations.at(0) == 0 || populations.at(1) == 0) {
        return -1;
    }
    const auto low = static_cast<double>(populations.at(0));
    const auto high = static_cast<double>(populations.at(1));
    const auto total = low + high;
    const auto difference =
        (static_cast<double>(moments.at(0)) / low) - (static_cast<double>(moments.at(1)) / high);
    return (low / total) * (high / total) * difference * difference;
}
inline BinaryOtsuReference binary_otsu_reference(image::PlaneView<const std::uint16_t> codes) {
    std::array<std::uint64_t, binary_reference_bins> bins{};
    for (std::uint32_t y = 0; y < codes.height(); ++y) {
        for (const auto value : codes.row(y)) {
            ++bins.at(binary_reference_bin(value));
        }
    }
    if (std::ranges::count_if(bins, [](auto n) { return n != 0; }) == 1) {
        return {.threshold = 2047, .fallback = true};
    }
    std::array<double, binary_reference_bins> scores{};
    scores.fill(-1);
    double best = 0;
    for (std::size_t bin = 0; bin + 1 < bins.size(); ++bin) {
        if (bins.at(bin) != 0) {
            scores.at(bin) = binary_reference_score(bins, bin);
            best = std::max(best, scores.at(bin));
        }
    }
    constexpr double relative_tolerance = 1e-12;
    const auto tolerance = relative_tolerance * std::max(1.0, best);
    for (std::size_t bin = 0; bin + 1 < bins.size(); ++bin) {
        if (scores.at(bin) >= 0 && best - scores.at(bin) <= tolerance) {
            return {.threshold = static_cast<std::uint16_t>(bin)};
        }
    }
    require(false, "independent bin-merged Otsu definition has an admissible split");
    return {};
}
// Independent direct windows; this fast oracle's declared domain keeps all products below2^64.
inline std::uint8_t binary_sauvola_reference(image::PlaneView<const std::uint16_t> codes,
                                             const methods::Sauvola& method, std::uint32_t x,
                                             std::uint32_t y) {
    constexpr std::uint32_t maximum_reference_window = 17;
    require(method.window() <= maximum_reference_window, "small direct Q16 oracle domain");
    const auto radius = static_cast<std::int64_t>(method.window() / 2);
    std::uint64_t sum = 0;
    std::uint64_t squares = 0;
    for (auto dy = -radius; dy <= radius; ++dy) {
        const auto row =
            codes.row(mirror_coordinate(static_cast<std::int64_t>(y) + dy, codes.height()));
        for (auto dx = -radius; dx <= radius; ++dx) {
            const std::uint64_t value =
                row.subspan(mirror_coordinate(static_cast<std::int64_t>(x) + dx, codes.width()), 1)
                    .front();
            sum += value;
            squares += value * value;
        }
    }
    const std::uint64_t count = std::uint64_t{method.window()} * method.window();
    const auto n = static_cast<double>(count);
    const auto deviation = std::sqrt(static_cast<double>((count * squares) - (sum * sum))) / n;
    const auto threshold =
        (static_cast<double>(sum) / n) *
        (1.0 + (method.k() * ((deviation / (binary_reference_max * method.r())) - 1.0)));
    return static_cast<double>(codes.row(y).subspan(x, 1).front()) <= threshold ? 0 : 1;
}
} // namespace docenhance::tests
