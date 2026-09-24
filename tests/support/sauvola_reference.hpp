// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/image/plane.hpp"

#include <cmath>
#include <cstdint>

namespace docenhance::tests {
// Deliberately independent of numeric::reflect101 and the rolling production implementation.
inline std::uint32_t mirror_coordinate(std::int64_t position, std::uint32_t length) {
    if (length == 1) {
        return 0;
    }
    const auto last = static_cast<std::int64_t>(length) - 1;
    const auto period = 2 * last;
    auto folded = position % period;
    if (folded < 0) {
        folded += period;
    }
    return static_cast<std::uint32_t>(folded > last ? period - folded : folded);
}
struct SauvolaReference {
    image::PlaneView<const std::uint8_t> source;
    std::uint32_t window = 3;
    double k = 0.2;
    double r = 0.5;
    [[nodiscard]] std::uint8_t at(std::uint32_t x, std::uint32_t y) const {
        const auto radius = static_cast<std::int64_t>(window / 2);
        std::uint64_t sum = 0;
        std::uint64_t squares = 0;
        for (std::int64_t dy = -radius; dy <= radius; ++dy) {
            const auto row =
                source.row(mirror_coordinate(static_cast<std::int64_t>(y) + dy, source.height()));
            for (std::int64_t dx = -radius; dx <= radius; ++dx) {
                const auto column =
                    mirror_coordinate(static_cast<std::int64_t>(x) + dx, source.width());
                const auto sample = static_cast<std::uint64_t>(row.subspan(column, 1).front());
                sum += sample;
                squares += sample * sample;
            }
        }
        constexpr double byte_scale = 255.0;
        const auto count = static_cast<std::uint64_t>(window) * window;
        const auto n = static_cast<double>(count);
        const double mean = static_cast<double>(sum) / n;
        const double deviation =
            std::sqrt(static_cast<double>((count * squares) - (sum * sum))) / n;
        const double threshold = mean * (1.0 + (k * ((deviation / (byte_scale * r)) - 1.0)));
        return source.row(y).subspan(x, 1).front() <= threshold ? 0 : UINT8_MAX;
    }
};
} // namespace docenhance::tests
