// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "restoration_working_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <utility>
#include <vector>
namespace docenhance::tests {
inline std::uint32_t restoration_reference_fold(std::int64_t x, std::uint32_t n) {
    if (n == 1) {
        return 0;
    }
    while (x < 0 || std::cmp_greater_equal(x, n)) {
        x = x < 0 ? -x : (2 * static_cast<std::int64_t>(n)) - 2 - x;
    }
    return static_cast<std::uint32_t>(x);
}
inline std::uint32_t restoration_reference_extent(std::uint32_t n) {
    for (;; ++n) {
        auto v = n;
        for (const auto factor : {2U, 3U, 5U}) {
            while (v % factor == 0) {
                v /= factor;
            }
        }
        if (v == 1) {
            return n;
        }
    }
}
inline std::complex<double> restoration_reference_wave(std::uint64_t phase, std::uint64_t n,
                                                       bool backward = false) {
    const auto angle = (backward ? 2 : -2) * std::numbers::pi * static_cast<double>(phase % n) /
                       static_cast<double>(n);
    return {std::cos(angle), std::sin(angle)};
}
inline double restoration_reference_mean(std::span<const double> values) {
    double sum = 0;
    double correction = 0;
    for (const auto value : values) {
        const auto next = sum + value;
        correction +=
            std::abs(sum) >= std::abs(value) ? (sum - next) + value : (value - next) + sum;
        sum = next;
    }
    return (sum + correction) / static_cast<double>(values.size());
}
inline std::vector<std::complex<double>> restoration_reference_dft(std::span<const double> input,
                                                                   std::uint32_t width,
                                                                   std::uint32_t height) {
    std::vector<std::complex<double>> rows(std::size_t{width} * height);
    std::vector<std::complex<double>> result(rows.size());
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t k = 0; k < width; ++k) {
            std::complex<double> value{};
            for (std::uint32_t x = 0; x < width; ++x) {
                value += input[(std::size_t{y} * width) + x] *
                         restoration_reference_wave(std::uint64_t{k} * x, width);
            }
            rows.at((std::size_t{y} * width) + k) = value;
        }
    }
    for (std::uint32_t x = 0; x < width; ++x) {
        for (std::uint32_t k = 0; k < height; ++k) {
            std::complex<double> value{};
            for (std::uint32_t y = 0; y < height; ++y) {
                value += rows.at((std::size_t{y} * width) + x) *
                         restoration_reference_wave(std::uint64_t{k} * y, height);
            }
            result.at((std::size_t{k} * width) + x) = value;
        }
    }
    return result;
}
struct RestorationReference {
    std::vector<double> output;
    methods::PixelCounts counts{};
    methods::RestorationTransform transform{};
    std::uint64_t context_samples{};
    std::uint64_t corrected_samples{};
    std::uint64_t raw_low_samples{}, raw_high_samples{}, blended_low_samples{},
        blended_high_samples{};
    double minimum_target_margin = std::numeric_limits<double>::infinity();
};
inline std::complex<double> restoration_reference_psf(const methods::ResolvedPsf& psf,
                                                      std::uint32_t kx, std::uint32_t ky,
                                                      std::uint32_t width, std::uint32_t height) {
    std::complex<double> result{};
    for (std::uint32_t y = 0; y < psf.height; ++y) {
        for (std::uint32_t x = 0; x < psf.width; ++x) {
            const auto dx = (std::int64_t{x} - (psf.width / 2) + width) % width;
            const auto dy = (std::int64_t{y} - (psf.height / 2) + height) % height;
            const auto phase = (std::uint64_t{kx} * static_cast<std::uint64_t>(dx) * height) +
                               (std::uint64_t{ky} * static_cast<std::uint64_t>(dy) * width);
            result += psf.coefficients.at((std::size_t{y} * psf.width) + x) *
                      restoration_reference_wave(phase, std::uint64_t{width} * height);
        }
    }
    return result;
}
inline double restoration_reference_inverse(std::span<const std::complex<double>> spectrum,
                                            std::uint32_t width, std::uint32_t height,
                                            std::uint32_t x, std::uint32_t y) {
    std::complex<double> sum{};
    for (std::uint32_t ky = 0; ky < height; ++ky) {
        for (std::uint32_t kx = 0; kx < width; ++kx) {
            sum += spectrum[(std::size_t{ky} * width) + kx] *
                   restoration_reference_wave((std::uint64_t{kx} * x * height) +
                                                  (std::uint64_t{ky} * y * width),
                                              std::uint64_t{width} * height, true);
        }
    }
    return sum.real() / static_cast<double>(std::uint64_t{width} * height);
}
inline std::vector<double> restoration_reference_pad(image::PlaneView<double> yplane,
                                                     methods::RestorationTransform shape) {
    const auto width = shape.width;
    const auto height = shape.height;
    const auto guard = shape.guard;
    std::vector<double> padded(std::size_t{width} * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            padded.at((std::size_t{y} * width) + x) =
                yplane.row(restoration_reference_fold(std::int64_t{y} - guard, yplane.height()))
                    .subspan(restoration_reference_fold(std::int64_t{x} - guard, yplane.width()), 1)
                    .front();
        }
    }
    return padded;
}
inline void restoration_reference_filter(std::span<std::complex<double>> transformed,
                                         const methods::ResolvedPsf& psf,
                                         methods::RestorationTransform shape,
                                         double regularization) {
    const auto width = shape.width;
    const auto height = shape.height;
    for (std::uint32_t ky = 0; ky < height; ++ky) {
        for (std::uint32_t kx = 0; kx < width; ++kx) {
            const auto kernel = restoration_reference_psf(psf, kx, ky, width, height);
            transformed.subspan((std::size_t{ky} * width) + kx, 1).front() =
                std::conj(kernel) * transformed.subspan((std::size_t{ky} * width) + kx, 1).front() /
                (std::norm(kernel) + regularization);
        }
    }
}
inline RestorationReference restoration_working_reference(RestorationWorkingFixture& fixture,
                                                          const methods::Wiener& method,
                                                          const methods::ResolvedPsf& psf) {
    const auto yplane = fixture.luminance.view();
    const auto guard = std::max(32U, 4 * std::max(psf.width / 2, psf.height / 2));
    const auto width = restoration_reference_extent(yplane.width() + (2 * guard));
    const auto height = restoration_reference_extent(yplane.height() + (2 * guard));
    const methods::RestorationTransform shape{
        .guard = guard,
        .width = width,
        .height = height,
    };
    auto padded = restoration_reference_pad(yplane, shape);
    const auto mean = restoration_reference_mean(padded);
    for (auto& value : padded) {
        value -= mean;
    }
    auto transformed = restoration_reference_dft(padded, width, height);
    restoration_reference_filter(transformed, psf, shape, method.parameters().k);
    RestorationReference result;
    result.output.assign(yplane.storage().begin(), yplane.storage().end());
    result.transform = methods::RestorationTransform{
        .guard = guard,
        .width = width,
        .height = height,
        .padded_mean = mean,
    };
    for (std::uint32_t y = 0; y < yplane.height(); ++y) {
        for (std::uint32_t x = 0; x < yplane.width(); ++x) {
            if (fixture.protection.view().row(y).subspan(x, 1).front() != 0) {
                ++result.counts.protected_samples;
                continue;
            }
            ++result.counts.eligible;
            ++result.counts.evaluated;
            const auto original = yplane.row(y).subspan(x, 1).front();
            const auto candidate = mean + restoration_reference_inverse(transformed, width, height,
                                                                        x + guard, y + guard);
            const auto blend = method.parameters().blend;
            const auto target = ((1 - blend) * original) + (blend * candidate);
            result.minimum_target_margin =
                std::min(result.minimum_target_margin, std::abs(target - original));
            result.corrected_samples += static_cast<std::uint64_t>(target != original);
            result.raw_low_samples += static_cast<std::uint64_t>(candidate < 0);
            result.raw_high_samples += static_cast<std::uint64_t>(candidate > 1);
            result.blended_low_samples += static_cast<std::uint64_t>(target < 0);
            result.blended_high_samples += static_cast<std::uint64_t>(target > 1);
            const auto value = std::clamp(target, 0.0, 1.0);
            if (value != original) {
                ++result.counts.changed;
                result.output.at((std::size_t{y} * yplane.row_pitch()) + x) = value;
            }
        }
    }
    result.context_samples = result.counts.eligible + result.counts.protected_samples;
    return result;
}
} // namespace docenhance::tests
