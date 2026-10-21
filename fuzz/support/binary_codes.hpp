// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
namespace docenhance::fuzz {
inline std::uint8_t binary_byte(std::span<const std::uint8_t> bytes, std::size_t index) {
    return index < bytes.size() ? bytes.subspan(index, 1).front() : 0;
}
inline image::Plane<std::uint16_t> binary_codes(std::span<const std::uint8_t> bytes,
                                                image::Extent extent, std::size_t header,
                                                core::Budget& budget) {
    auto result =
        image::Plane<std::uint16_t>::allocate(budget, extent.width, extent.height).value();
    constexpr std::uint16_t padding = 0x3141;
    std::ranges::fill(result.view().storage(), padding);
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        const auto row = result.view().row(y);
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const auto index = header + (2 * ((std::size_t{y} * extent.width) + x));
            row.subspan(x, 1).front() =
                static_cast<std::uint16_t>((std::uint32_t{binary_byte(bytes, index)} << 8U) |
                                           std::uint32_t{binary_byte(bytes, index + 1)});
        }
    }
    return result;
}
} // namespace docenhance::fuzz
