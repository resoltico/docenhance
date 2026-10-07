// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "linear_fixture.hpp"
#include "require.hpp"
#include "sharpen_reference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
namespace docenhance::tests {
inline void sharpen_threshold_cases() {
    const auto threshold =
        methods::Unsharp::create({.sigma = 0.8, .amount = 2, .threshold = 255.0 / 32}).value();
    for (const auto blurred : std::array{0.5 - (1.0 / 32), 0.5 + (1.0 / 32)}) {
        require(methods::unsharp_candidate(0.5, blurred, threshold).value() == 0.5,
                "soft-threshold equality retains exact entering perceptual intensity");
        const auto inside = std::nextafter(blurred, 0.5);
        require(methods::unsharp_candidate(0.5, inside, threshold).value() == 0.5,
                "next representable value inside soft threshold is identity");
        const auto outside = std::nextafter(blurred, blurred < 0.5 ? 0.0 : 1.0);
        const auto candidate = methods::unsharp_candidate(0.5, outside, threshold).value();
        require(blurred < 0.5 ? candidate > 0.5 : candidate < 0.5,
                "next representable value outside soft threshold has signed residual");
    }
    require(!methods::Unsharp::create({.sigma = 0, .amount = 0}),
            "zero amount does not legalize invalid Gaussian sigma");
    require(!methods::unsharp_candidate(std::numeric_limits<double>::quiet_NaN(), 0.5, threshold),
            "nonfinite unsharp intensity is numerical failure");
}
inline std::vector<double> sharpen_reference_source(LinearFixture& source) {
    std::vector<double> entering;
    for (std::uint32_t y = 0; y < 3; ++y) {
        for (std::uint32_t x = 0; x < 7; ++x) {
            const auto f = static_cast<double>(1 + (((x * 5) + (y * 3)) % 9)) / 10;
            entering.push_back(f);
            std::ranges::fill(
                source.row(y).subspan(std::size_t{x} * image::rgb_channels, image::rgb_channels),
                image::srgb_decode(f).value());
        }
    }
    return entering;
}
inline void sharpen_gaussian_case(LinearFixture& source, const std::vector<double>& entering,
                                  double sigma, core::Budget& budget) {
    const auto method =
        methods::Unsharp::create({.sigma = sigma, .amount = 1.5, .threshold = 0.5}).value();
    const auto expected = sharpen_reference(
        entering, {.width = 7, .height = 3, .sigma = sigma, .amount = 1.5, .threshold = 0.5});
    methods::SharpenReport report;
    auto model = methods::SharpenModel::prepare(source, {}, method,
                                                {budget, {}, report, image::RowUse::output});
    require(model.has_value(), "standalone Gaussian context preparation succeeds");
    for (std::uint32_t y = 0; y < 3; ++y) {
        const auto original = source.row(y);
        auto output = std::vector<double>(original.begin(), original.end());
        require(model->apply({.row = y}, output, {}, report, {}).has_value(),
                "standalone unsharp reconstruction succeeds");
        for (std::uint32_t x = 0; x < 7; ++x) {
            const auto actual = image::srgb_encode(output.at(std::size_t{x} * image::rgb_channels));
            require(actual && std::abs(*actual - std::clamp(expected.at((std::size_t{y} * 7) + x),
                                                            0.0, 1.0)) < 2e-12,
                    "standalone model agrees with independent 2D Gaussian");
        }
    }
    require(report.context_samples == 21 && report.evaluated_samples == 21,
            "standalone context and reconstruction observe samples once");
}
inline void sharpening_cases() {
    sharpen_threshold_cases();
    constexpr std::size_t limit = std::size_t{2} * 1024 * 1024;
    core::Budget source_budget{limit};
    LinearFixture source{source_budget, {.width = 7, .height = 3}};
    const auto entering = sharpen_reference_source(source);
    for (const auto sigma : std::array{0.3, 0.8, 3.0}) {
        core::Budget budget{limit};
        sharpen_gaussian_case(source, entering, sigma, budget);
        require(budget.used() == 0, "standalone Gaussian storage refunds its charged budget");
    }
}
} // namespace docenhance::tests
