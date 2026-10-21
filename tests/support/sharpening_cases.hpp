// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/unsharp.hpp"
#include "require.hpp"
#include "sharpen_reference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace docenhance::tests {
inline void sharpen_threshold_cases() {
    const auto threshold =
        methods::Unsharp::create({.sigma = 0.8, .amount = 2, .threshold = 255.0 / 32});
    require(threshold.has_value(), "soft-threshold method parameters admit");
    for (const auto blurred : std::array{0.5 - (1.0 / 32), 0.5 + (1.0 / 32)}) {
        require(methods::unsharp_candidate(0.5, blurred, *threshold) == 0.5,
                "soft-threshold equality retains exact entering perceptual intensity");
        const auto inside = std::nextafter(blurred, 0.5);
        require(methods::unsharp_candidate(0.5, inside, *threshold) == 0.5,
                "next representable value inside soft threshold is identity");
        const auto outside = std::nextafter(blurred, blurred < 0.5 ? 0.0 : 1.0);
        const auto candidate = methods::unsharp_candidate(0.5, outside, *threshold);
        require(blurred < 0.5 ? candidate > 0.5 : candidate < 0.5,
                "next representable value outside soft threshold has signed residual");
    }
    require(!methods::Unsharp::create({.sigma = 0, .amount = 0}),
            "zero amount does not legalize invalid Gaussian sigma");
}
inline std::vector<double> sharpen_reference_source(image::Plane<double>& source) {
    std::vector<double> entering;
    for (std::uint32_t y = 0; y < 3; ++y) {
        for (std::uint32_t x = 0; x < 7; ++x) {
            const auto f = static_cast<double>(1 + (((x * 5) + (y * 3)) % 9)) / 10;
            entering.push_back(f);
            source.view().row(y).subspan(x, 1).front() = image::srgb_decode(f);
        }
    }
    return entering;
}
inline void sharpen_gaussian_case(image::Plane<double>& source, const std::vector<double>& entering,
                                  double sigma, core::Budget& budget) {
    const auto method = methods::Unsharp::create({.sigma = sigma, .amount = 1.5, .threshold = 0.5});
    require(method.has_value(), "standalone Gaussian parameters admit");
    const auto expected = sharpen_reference(
        entering, {.width = 7, .height = 3, .sigma = sigma, .amount = 1.5, .threshold = 0.5});
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    require(concurrency.has_value(), "standalone Gaussian scheduler resolves");
    const exec::Scheduler scheduler{*concurrency};
    const auto facts = methods::apply(*method, {source.view(), {}, budget, scheduler});
    require(facts.has_value(), "standalone eager unsharp application succeeds");
    for (std::uint32_t y = 0; y < 3; ++y) {
        for (std::uint32_t x = 0; x < 7; ++x) {
            const auto actual = source.view().row(y).subspan(x, 1).front();
            const auto index = (std::size_t{y} * 7) + x;
            const auto expected_linear =
                sharpen_reference_linear_output(sharpen_reference_srgb_decode(entering.at(index)),
                                                entering.at(index), expected.at(index));
            require(std::isfinite(actual) && actual >= 0 && actual <= 1,
                    "standalone eager sharpening produces finite unit Y before encoding");
            require(std::abs(actual - expected_linear) < 2e-12,
                    "standalone eager application agrees with independent 2D Gaussian");
        }
    }
    require(facts->context_samples == 21 && facts->counts.evaluated == 21,
            "standalone context and mapping observe samples once");
}
inline void sharpening_cases() {
    sharpen_threshold_cases();
    constexpr std::size_t limit = std::size_t{2} * 1024 * 1024;
    core::Budget source_budget{limit};
    auto source = image::Plane<double>::allocate(source_budget, 7, 3);
    require(source.has_value(), "standalone source owner allocates");
    for (const auto sigma : std::array{0.3, 0.8, 3.0}) {
        const auto entering = sharpen_reference_source(*source);
        core::Budget budget{limit};
        sharpen_gaussian_case(*source, entering, sigma, budget);
        require(budget.used() == 0, "standalone Gaussian storage refunds its charged budget");
    }
}
} // namespace docenhance::tests
