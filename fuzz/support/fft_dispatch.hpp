// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fft/fft.hpp"
#include "docenhance/image/plane.hpp"
#include "oracle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <span>
#include <vector>
namespace docenhance::fuzz {
inline constexpr std::size_t dispatch_samples = 8;
inline void check_fft_dispatch(std::size_t position) {
    core::Budget budget{std::size_t{1024} * 1024};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    std::array<double, dispatch_samples> source{};
    std::array<double, dispatch_samples> output{};
    source.at(position) = 1;
    const image::PlaneShape shape{
        .width = dispatch_samples,
        .height = 1,
        .stride = sizeof(source),
    };
    {
        auto spectrum = fft::forward(image::PlaneView<const double>::create(source, shape).value(),
                                     budget, scheduler);
        require(spectrum.has_value(), "actual binary64 public FFT forward");
        const auto row = spectrum->row(0);
        for (std::size_t k = 0; k < row.size(); ++k) {
            const auto phase = -2 * std::numbers::pi * static_cast<double>(k * position) /
                               static_cast<double>(dispatch_samples);
            const auto error = std::abs(row[k] - std::polar(1.0, phase));
            require(std::isfinite(error) && error < 1e-12,
                    "independent public FFT impulse phase/DC");
        }
        // Author inverse input independently: DC plus implicit conjugate frequencies gives sine.
        std::ranges::fill(spectrum->storage(), fft::Complex{-0.0, -0.0});
        row.front() = {4, 0};
        row[1] = {0, -1};
        const auto backing = spectrum->storage();
        const auto bytes = std::as_bytes(backing);
        const std::vector<std::byte> before(bytes.begin(), bytes.end());
        require(fft::inverse(*spectrum, image::PlaneView<double>::create(output, shape).value(),
                             budget, scheduler)
                    .has_value(),
                "actual binary64 public FFT inverse");
        require(std::ranges::equal(bytes, before) && budget.used() == spectrum->size_bytes(),
                "public inverse preserves complete const spectrum and refunds temporaries");
        for (std::size_t x = 0; x < output.size(); ++x) {
            const auto phase = 2 * std::numbers::pi * static_cast<double>(x) /
                               static_cast<double>(dispatch_samples);
            const auto error = std::abs(output.at(x) - (0.5 + (0.25 * std::sin(phase))));
            require(std::isfinite(error) && error < 1e-12,
                    "independent public inverse phase/DC/scale");
        }
    }
    require(budget.used() == 0, "public FFT owners refund all charged storage");
}
} // namespace docenhance::fuzz
