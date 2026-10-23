// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/restoration.hpp"
#include "gamma_fixture.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace docenhance::tests {
inline constexpr std::size_t restoration_test_scratch = std::size_t{64} * 1024 * 1024;
struct RestorationWorkingFixture {
    RestorationWorkingFixture(std::uint32_t width, std::uint32_t height,
                              std::size_t extra = restoration_test_scratch)
        : budget(gamma_fixture_bytes(width, height) + extra),
          luminance(image::Plane<double>::allocate(budget, width, height).value()),
          protection(image::Plane<std::uint8_t>::allocate(budget, width, height).value()) {
        std::ranges::fill(luminance.view().storage(), -1);
        std::ranges::fill(protection.view().storage(), std::uint8_t{255});
        constexpr double base = 0.25;
        constexpr double step = 0.04;
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                luminance.view().row(y).subspan(x, 1).front() =
                    base + (step * static_cast<double>(((13 * x) + (7 * y)) % 11));
                protection.view().row(y).subspan(x, 1).front() = 0;
            }
        }
    }
    core::Budget budget;
    image::Plane<double> luminance;
    image::Plane<std::uint8_t> protection;
};
inline methods::ResolvedPsf restoration_gaussian(methods::GaussianPsf parameters = {}) {
    const auto side = methods::psf_side(parameters).value();
    methods::ResolvedPsf psf{
        .width = side,
        .height = side,
        .coefficients = std::vector<double>(std::size_t{side} * side),
    };
    methods::gaussian_psf(parameters, psf.coefficients).value();
    const auto centroid = methods::psf_centroid(side, side, psf.coefficients).value();
    psf.centroid_x = centroid.front();
    psf.centroid_y = centroid.back();
    return psf;
}
} // namespace docenhance::tests
