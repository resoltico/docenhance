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
struct MorphologyWorkingFixture {
    MorphologyWorkingFixture(std::uint32_t width, std::uint32_t height, std::size_t scratch)
        : budget(gamma_fixture_bytes(width, height) + scratch),
          luminance(image::Plane<double>::allocate(budget, width, height).value()),
          protection(image::Plane<std::uint8_t>::allocate(budget, width, height).value()) {
        std::ranges::fill(luminance.view().storage(), -1);
        std::ranges::fill(protection.view().storage(), std::uint8_t{255});
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                luminance.view().row(y).subspan(x, 1).front() =
                    static_cast<double>(128 + (((17 * x) + (13 * y)) % 640)) / 1024;
                protection.view().row(y).subspan(x, 1).front() = 0;
            }
        }
    }
    core::Budget budget;
    image::Plane<double> luminance;
    image::Plane<std::uint8_t> protection;
};
inline std::size_t morphology_test_plane(std::uint32_t width, std::uint32_t height,
                                         std::size_t sample_bytes) {
    return image::plane_bytes(image::plane_shape(width, height, sample_bytes).value()).value();
}
inline std::size_t morphology_test_peak(std::uint32_t width, std::uint32_t height,
                                        std::uint32_t radius, std::uint64_t eligible) {
    const auto p = morphology_test_plane(width, height, sizeof(double));
    const auto s = morphology_test_plane(
        static_cast<std::uint32_t>(std::min(eligible, std::uint64_t{1048576})), 1, sizeof(double));
    const auto q = morphology_test_plane((2 * radius) + 1, std::min(64U, std::max(width, height)),
                                         sizeof(std::uint64_t));
    const auto k = morphology_test_plane((2 * (((3 * radius) + 1) / 2)) + 1, 1, sizeof(double));
    return std::max({s, (2 * p) + q, (2 * p) + k, p + s});
}
inline constexpr std::size_t morphology_test_scratch = std::size_t{4} * 1024 * 1024;
} // namespace docenhance::tests
