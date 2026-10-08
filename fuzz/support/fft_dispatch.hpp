// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "oracle.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <span>
#include <type_traits>
namespace docenhance::fuzz {
inline constexpr std::size_t dispatch_samples = 8;
template <typename Sample> constexpr double dispatch_tolerance() {
    return std::is_same_v<Sample, float> ? 2e-6 : 1e-12;
}
template <typename Sample>
void check_complex(std::span<const Sample> samples, std::size_t index,
                   std::complex<double> expected) {
    const std::complex<double> actual{static_cast<double>(samples[index * 2]),
                                      static_cast<double>(samples[(index * 2) + 1])};
    require(std::abs(actual - expected) < dispatch_tolerance<Sample>(),
            "independent typed FFT complex phase/DC/scale");
}
template <typename Sample> void real_dispatch(std::size_t position) {
    constexpr auto real_type = std::is_same_v<Sample, float> ? CV_32FC1 : CV_64FC1;
    constexpr auto complex_type = std::is_same_v<Sample, float> ? CV_32FC2 : CV_64FC2;
    std::array<Sample, dispatch_samples> source{};
    std::array<Sample, dispatch_samples * 2> spectrum{};
    std::array<Sample, dispatch_samples> output{};
    source.at(position) = 1;
    const cv::Mat input(1, static_cast<int>(dispatch_samples), real_type, source.data());
    cv::Mat frequency(1, static_cast<int>(dispatch_samples), complex_type, spectrum.data());
    cv::Mat result(1, static_cast<int>(dispatch_samples), real_type, output.data());
    cv::dft(input, frequency, cv::DFT_COMPLEX_OUTPUT);
    for (std::size_t k = 0; k < dispatch_samples; ++k) {
        const auto phase = -2 * std::numbers::pi * static_cast<double>(k * position) /
                           static_cast<double>(dispatch_samples);
        check_complex<Sample>(spectrum, k, std::polar(1.0, phase));
    }
    // Independent inverse input: DC plus conjugate imaginary frequencies yields a sine wave.
    spectrum.fill(0);
    spectrum.at(0) = static_cast<Sample>(dispatch_samples * 0.5);
    spectrum.at(3) = static_cast<Sample>(-static_cast<double>(dispatch_samples) * 0.125);
    spectrum.at(((dispatch_samples - 1) * 2) + 1) = static_cast<Sample>(dispatch_samples * 0.125);
    constexpr auto inverse_flags = cv::DFT_INVERSE + cv::DFT_REAL_OUTPUT + cv::DFT_SCALE;
    cv::dft(frequency, result, inverse_flags);
    for (std::size_t x = 0; x < dispatch_samples; ++x) {
        const auto phase =
            2 * std::numbers::pi * static_cast<double>(x) / static_cast<double>(dispatch_samples);
        const auto expected = 0.5 + (0.25 * std::sin(phase));
        require(std::abs(static_cast<double>(output.at(x)) - expected) <
                    dispatch_tolerance<Sample>(),
                "independent typed FFT real inverse phase/DC/scale");
    }
}
template <typename Sample> void complex_dispatch(std::size_t position) {
    constexpr auto complex_type = std::is_same_v<Sample, float> ? CV_32FC2 : CV_64FC2;
    std::array<Sample, dispatch_samples * 2> source{};
    std::array<Sample, dispatch_samples * 2> spectrum{};
    std::array<Sample, dispatch_samples * 2> output{};
    source.at(position * 2) = 1;
    source.at((position * 2) + 1) = static_cast<Sample>(0.25);
    const cv::Mat input(1, static_cast<int>(dispatch_samples), complex_type, source.data());
    cv::Mat frequency(1, static_cast<int>(dispatch_samples), complex_type, spectrum.data());
    cv::Mat result(1, static_cast<int>(dispatch_samples), complex_type, output.data());
    cv::dft(input, frequency);
    for (std::size_t k = 0; k < dispatch_samples; ++k) {
        const auto phase = -2 * std::numbers::pi * static_cast<double>(k * position) /
                           static_cast<double>(dispatch_samples);
        check_complex<Sample>(spectrum, k, std::complex<double>{1, 0.25} * std::polar(1.0, phase));
    }
    // This inverse spectrum is authored analytically, not obtained from the forward transform.
    spectrum.fill(0);
    spectrum.at(0) = static_cast<Sample>(dispatch_samples * 0.5);
    spectrum.at(2) = static_cast<Sample>(dispatch_samples * 0.25);
    spectrum.at(3) = static_cast<Sample>(dispatch_samples * 0.5);
    constexpr auto inverse_flags = cv::DFT_INVERSE + cv::DFT_SCALE;
    cv::dft(frequency, result, inverse_flags);
    for (std::size_t x = 0; x < dispatch_samples; ++x) {
        const auto phase =
            2 * std::numbers::pi * static_cast<double>(x) / static_cast<double>(dispatch_samples);
        const auto expected = std::complex<double>{0.5, 0} +
                              (std::complex<double>{0.25, 0.5} * std::polar(1.0, phase));
        check_complex<Sample>(output, x, expected);
    }
}
inline void check_fft_dispatch(std::size_t position) {
    real_dispatch<float>(position);
    complex_dispatch<float>(position);
    real_dispatch<double>(position);
    complex_dispatch<double>(position);
}
} // namespace docenhance::fuzz
