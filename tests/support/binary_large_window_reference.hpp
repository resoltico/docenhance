// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "require.hpp"
#include "sauvola_reference.hpp"

#include <cmath>
#include <cstdint>
#include <vector>
namespace docenhance::tests {
// Independent multiplicities, found by enumerating each axis rather than production cycle/slide
// recurrences. This oracle is deliberately only for the two-level65534/65535 signal: variance
// equals ones*(N-ones), so its exact numerator fits uint64 even at window4095.
inline std::vector<std::uint64_t>
binary_axis_multiplicity(std::uint32_t center, std::uint32_t extent, std::uint32_t window) {
    std::vector<std::uint64_t> result(extent);
    const auto radius = static_cast<std::int64_t>(window / 2);
    for (auto offset = -radius; offset <= radius; ++offset) {
        ++result.at(mirror_coordinate(static_cast<std::int64_t>(center) + offset, extent));
    }
    return result;
}
inline std::uint8_t binary_large_window_reference(image::PlaneView<const std::uint16_t> source,
                                                  const methods::Sauvola& method, std::uint32_t x,
                                                  std::uint32_t y) {
    constexpr std::uint16_t base = 65534;
    constexpr double scale = 65535;
    const auto horizontal = binary_axis_multiplicity(x, source.width(), method.window());
    const auto vertical = binary_axis_multiplicity(y, source.height(), method.window());
    std::uint64_t ones = 0;
    for (std::uint32_t row = 0; row < source.height(); ++row) {
        const auto values = source.row(row);
        for (std::uint32_t column = 0; column < source.width(); ++column) {
            const auto value = values.subspan(column, 1).front();
            require(value == base || value == base + 1,
                    "independent large-window two-level domain");
            ones +=
                static_cast<std::uint64_t>(value - base) * horizontal.at(column) * vertical.at(row);
        }
    }
    const std::uint64_t count = std::uint64_t{method.window()} * method.window();
    const auto sum = (count * base) + ones;
    const auto variance = ones * (count - ones);
    const auto n = static_cast<double>(count);
    const auto mean = static_cast<double>(sum) / n;
    const auto deviation = std::sqrt(static_cast<double>(variance)) / n;
    const auto threshold = mean * (1.0 + (method.k() * ((deviation / (scale * method.r())) - 1.0)));
    return static_cast<double>(source.row(y).subspan(x, 1).front()) <= threshold ? 0 : 1;
}
} // namespace docenhance::tests
