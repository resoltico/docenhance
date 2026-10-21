// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fft/fft.hpp"
#include "docenhance/image/plane.hpp"
#include "require.hpp"

#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>
namespace docenhance::tests {
inline constexpr std::size_t fft_test_budget = std::size_t{64} * 1024 * 1024;
struct FftReal {
    FftReal(std::uint32_t w, std::uint32_t h, std::size_t extra, bool minimal)
        : width(w), height(h), pitch(w + extra),
          values(minimal ? ((h - 1) * pitch) + w : h * pitch, -777) {}
    image::PlaneView<double> view() & {
        return image::PlaneView<double>::create(
                   values, {.width = width, .height = height, .stride = pitch * sizeof(double)})
            .value();
    }
    image::PlaneView<double> view() && = delete;
    void fill() {
        constexpr double step = 0.17;
        constexpr double divisor = 19;
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                view().row(y).subspan(x, 1).front() =
                    std::sin(step * static_cast<double>(x + (3 * y))) +
                    (static_cast<double>(((7 * x) + (11 * y)) % 19) / divisor);
            }
        }
    }
    std::uint32_t width, height;
    std::size_t pitch;
    std::vector<double> values;
};
template <typename T> std::vector<std::byte> fft_bits(std::span<const T> values) {
    const auto bytes = std::as_bytes(values);
    return {bytes.begin(), bytes.end()};
}
inline fft::Complex fft_direct(FftReal& input, std::uint32_t kx, std::uint32_t ky) {
    fft::Complex result{};
    for (std::uint32_t y = 0; y < input.height; ++y) {
        for (std::uint32_t x = 0; x < input.width; ++x) {
            const auto samples = std::uint64_t{input.width} * input.height;
            const auto phase =
                ((std::uint64_t{kx} * x * input.height) + (std::uint64_t{ky} * y * input.width)) %
                samples;
            const auto angle =
                -2 * std::numbers::pi * static_cast<double>(phase) / static_cast<double>(samples);
            result += input.view().row(y).subspan(x, 1).front() *
                      fft::Complex{std::cos(angle), std::sin(angle)};
        }
    }
    return result;
}
inline void fft_check_back(FftReal& original, FftReal& output) {
    for (std::size_t i = 0; i < output.values.size(); ++i) {
        const auto y = i / output.pitch;
        const auto x = i % output.pitch;
        if (x < output.width) {
            const auto expected =
                original.view().row(static_cast<std::uint32_t>(y)).subspan(x, 1).front();
            require(std::abs(output.values.at(i) - expected) < 1e-12,
                    "actual inverse has one correct normalization");
        } else {
            require(output.values.at(i) == -777,
                    "actual inverse preserves every output padding value");
        }
    }
}
inline void fft_transform_case(std::uint32_t width, std::uint32_t height, bool minimal) {
    FftReal input{width, height, 2, minimal};
    input.fill();
    FftReal output{width, height, 4, minimal};
    const auto original = fft_bits<double>(input.values);
    core::Budget budget{fft_test_budget};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(2, 2, 0, 0).value()};
    {
        const auto result = fft::forward(input.view().as_const(), budget, scheduler);
        require(
            result.has_value(),
            "actual public forward accepts sample-aligned borrowed stride and minimal final row");
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < result->shape().width; ++x) {
                require(std::abs(result->row(y).subspan(x, 1).front() - fft_direct(input, x, y)) <
                            1e-12,
                        "actual forward phase and half spectrum agree with independent direct "
                        "Fourier sums");
            }
        }
        require(fft_bits<double>(input.values) == original,
                "actual forward preserves entire input backing bits");
        const auto spectrum = fft_bits<fft::Complex>(result->storage());
        require(fft::inverse(*result, output.view(), budget, scheduler).has_value(),
                "actual scalar-axis inverse accepts real output borrowed stride");
        fft_check_back(input, output);
        require(fft_bits<fft::Complex>(result->storage()) == spectrum,
                "actual inverse charged work copy preserves complete const spectrum backing");
        require(budget.used() == result->size_bytes(),
                "only returned spectrum remains charged after joined inverse");
    }
    require(budget.used() == 0, "all actual FFT owners and native capacity leases refund");
}
} // namespace docenhance::tests
