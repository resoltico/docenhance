// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/image/plane.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace docenhance::tests {
inline constexpr std::uint16_t binary_padding = 0x3141;
inline constexpr std::uint8_t binary_sentinel = 17;
inline constexpr std::size_t binary_test_budget = std::size_t{4} * 1024 * 1024;
struct BinaryFixture {
    BinaryFixture(std::uint32_t width, std::uint32_t height,
                  std::size_t capacity = binary_test_budget)
        : budget(capacity),
          codes(image::Plane<std::uint16_t>::allocate(budget, width, height).value()),
          out(image::Plane<std::uint8_t>::allocate(budget, width, height).value()) {
        std::ranges::fill(codes.view().storage(), binary_padding);
        std::ranges::fill(out.view().storage(), binary_sentinel);
        constexpr std::uint64_t x_step = 131;
        constexpr std::uint64_t y_step = 977;
        constexpr std::uint64_t domain = std::uint64_t{1} << 16U;
        for (std::uint32_t y = 0; y < height; ++y) {
            const auto row = codes.view().row(y);
            for (std::uint32_t x = 0; x < width; ++x) {
                row.subspan(x, 1).front() = static_cast<std::uint16_t>(
                    ((std::uint64_t{x} * x_step) + (std::uint64_t{y} * y_step)) % domain);
            }
        }
    }
    core::Budget budget;
    image::Plane<std::uint16_t> codes;
    image::Plane<std::uint8_t> out;
};
} // namespace docenhance::tests
