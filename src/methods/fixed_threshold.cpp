// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/methods/fixed_threshold.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <cmath>
#include <cstdint>
#include <ranges>

namespace docenhance::methods {
namespace {
constexpr double max_sample = 255.0;
constexpr std::uint8_t black = 0U;
constexpr std::uint8_t white = 255U;
} // namespace

core::Result<void> fixed_threshold(image::PlaneView<const std::uint8_t> source,
                                   image::PlaneView<std::uint8_t> destination, double threshold) {
    if (source.empty() || source.width() != destination.width() ||
        source.height() != destination.height()) {
        return core::failure(core::ErrorCode::argument,
                             "B03 needs a non-empty source and a destination of the same size");
    }
    if (!std::isfinite(threshold) || threshold < 0.0 || threshold > 1.0) {
        return core::failure(core::ErrorCode::argument,
                             "B03 needs a finite threshold in the inclusive range [0,1]");
    }
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        const auto in = source.row(y);
        const auto out = destination.row(y);
        for (auto [input, output] : std::views::zip(in, out)) {
            output = static_cast<double>(input) / max_sample <= threshold ? black : white;
        }
    }
    return {};
}
} // namespace docenhance::methods
