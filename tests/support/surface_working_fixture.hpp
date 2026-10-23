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
inline constexpr std::size_t surface_test_scratch = std::size_t{16} * 1024 * 1024;
struct SurfaceWorkingFixture {
    SurfaceWorkingFixture(std::uint32_t width, std::uint32_t height, std::size_t scratch)
        : budget(gamma_fixture_bytes(width, height) + scratch),
          luminance(image::Plane<double>::allocate(budget, width, height).value()),
          protection(image::Plane<std::uint8_t>::allocate(budget, width, height).value()) {
        std::ranges::fill(luminance.view().storage(), -1);
        std::ranges::fill(protection.view().storage(), std::uint8_t{255});
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                luminance.view().row(y).subspan(x, 1).front() =
                    0.4 + (static_cast<double>(((17 * x) + (13 * y)) % 256) / 1024);
                protection.view().row(y).subspan(x, 1).front() = 0;
            }
        }
    }
    core::Budget budget;
    image::Plane<double> luminance;
    image::Plane<std::uint8_t> protection;
};
} // namespace docenhance::tests
