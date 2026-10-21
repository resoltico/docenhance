// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/image/plane.hpp"
#include "gamma_fixture.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace docenhance::tests {
inline std::size_t tvl1_plane_bytes(std::uint32_t width, std::uint32_t height) {
    return image::plane_bytes(image::plane_shape(width, height, sizeof(double)).value()).value();
}
struct Tvl1WorkingFixture {
    Tvl1WorkingFixture(std::uint32_t width, std::uint32_t height, std::size_t scratch)
        : budget(gamma_fixture_bytes(width, height) + scratch),
          luminance(image::Plane<double>::allocate(budget, width, height).value()),
          protection(image::Plane<std::uint8_t>::allocate(budget, width, height).value()) {
        std::ranges::fill(luminance.view().storage(), -1.0);
        std::ranges::fill(protection.view().storage(), 255);
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                luminance.view().row(y).subspan(x, 1).front() =
                    static_cast<double>(1 + (((17 * x) + (13 * y)) % 997)) / 1000;
                protection.view().row(y).subspan(x, 1).front() = 0;
            }
        }
    }
    core::Budget budget;
    image::Plane<double> luminance;
    image::Plane<std::uint8_t> protection;
};
} // namespace docenhance::tests
