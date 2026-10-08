// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/methods/restoration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <numbers>
#include <span>
namespace docenhance::methods {
core::Result<void> normalize_psf(std::uint32_t width, std::uint32_t height,
                                 std::span<double> coefficients) {
    constexpr auto maximum_side = psf_maximum_side;
    if (width < psf_minimum_side || height < psf_minimum_side || width > maximum_side ||
        height > maximum_side || width % 2 == 0 || height % 2 == 0 ||
        coefficients.size() != std::size_t{width} * height) {
        return core::failure(core::ErrorCode::argument, "Invalid PSF dimensions");
    }
    double sum = 0;
    double correction = 0;
    for (const auto value : coefficients) {
        if (!std::isfinite(value) || value < 0) {
            return core::failure(core::ErrorCode::argument,
                                 "PSF coefficients must be finite nonnegative");
        }
        const auto adjusted = value - correction;
        const auto next = sum + adjusted;
        correction = (next - sum) - adjusted;
        sum = next;
    }
    if (!std::isfinite(sum) || sum <= 0) {
        return core::failure(core::ErrorCode::argument, "PSF sum must be finite positive");
    }
    for (auto& value : coefficients) {
        value /= sum;
    }
    return {};
}
core::Result<void> gaussian_psf(GaussianPsf p, std::span<double> coefficients) {
    auto side = psf_side(p);
    if (!side) {
        return std::unexpected(side.error());
    }
    if (coefficients.size() != std::size_t{*side} * (*side)) {
        return core::failure(core::ErrorCode::argument, "Gaussian PSF buffer extent mismatch");
    }
    const auto radius = *side / 2;
    for (std::uint32_t y = 0; y < *side; ++y) {
        for (std::uint32_t x = 0; x < *side; ++x) {
            const auto dx = static_cast<double>(x) - radius;
            const auto dy = static_cast<double>(y) - radius;
            coefficients.subspan((std::size_t{y} * (*side)) + x, 1).front() =
                std::exp(-((dx * dx) + (dy * dy)) / (2 * p.sigma * p.sigma));
        }
    }
    return normalize_psf(*side, *side, coefficients);
}
core::Result<void> motion_psf(MotionPsf p, std::span<double> coefficients) {
    auto side = psf_side(p);
    if (!side) {
        return std::unexpected(side.error());
    }
    if (coefficients.size() != std::size_t{*side} * (*side)) {
        return core::failure(core::ErrorCode::argument, "Motion PSF buffer extent mismatch");
    }
    std::ranges::fill(coefficients, 0);
    const auto samples =
        std::max(MotionPsf::minimum_samples,
                 static_cast<std::uint32_t>(std::ceil(MotionPsf::samples_per_pixel * p.length)));
    const auto radians = p.angle * std::numbers::pi / MotionPsf::maximum_angle;
    const auto dx = std::cos(radians);
    const auto dy = std::sin(radians);
    const auto radius = *side / 2;
    for (std::uint32_t n = 0; n < samples; ++n) {
        const auto t = (((static_cast<double>(n) + 0.5) / samples) - 0.5) * p.length;
        const auto x = (t * dx) + radius;
        const auto y = (t * dy) + radius;
        const auto ix = static_cast<std::uint32_t>(std::floor(x));
        const auto iy = static_cast<std::uint32_t>(std::floor(y));
        const auto fx = x - ix;
        const auto fy = y - iy;
        for (std::uint32_t oy = 0; oy < 2; ++oy) {
            for (std::uint32_t ox = 0; ox < 2; ++ox) {
                const auto weight = (ox == 0 ? 1 - fx : fx) * (oy == 0 ? 1 - fy : fy);
                coefficients.subspan((std::size_t{iy + oy} * (*side)) + ix + ox, 1).front() +=
                    weight;
            }
        }
    }
    return normalize_psf(*side, *side, coefficients);
}
core::Result<std::array<double, 2>> psf_centroid(std::uint32_t width, std::uint32_t height,
                                                 std::span<const double> coefficients) {
    if (width < psf_minimum_side || height < psf_minimum_side || width > psf_maximum_side ||
        height > psf_maximum_side || width % 2 == 0 || height % 2 == 0 ||
        coefficients.size() != std::size_t{width} * height) {
        return core::failure(core::ErrorCode::argument, "Invalid PSF centroid extent");
    }
    std::array<double, 2> centroid{};
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto value = coefficients.subspan((std::size_t{y} * width) + x, 1).front();
            if (!std::isfinite(value) || value < 0) {
                return core::failure(core::ErrorCode::argument, "Invalid PSF centroid coefficient");
            }
            centroid.front() +=
                value * (static_cast<double>(x) - ((static_cast<double>(width) - 1) / 2));
            centroid.back() +=
                value * (static_cast<double>(y) - ((static_cast<double>(height) - 1) / 2));
        }
    }
    return centroid;
}
core::Result<void> validate_psf(const ResolvedPsf& psf) {
    constexpr auto maximum_side = psf_maximum_side;
    if (psf.width < psf_minimum_side || psf.height < psf_minimum_side || psf.width > maximum_side ||
        psf.height > maximum_side || psf.width % 2 == 0 || psf.height % 2 == 0 ||
        psf.coefficients.size() != std::size_t{psf.width} * psf.height) {
        return core::failure(core::ErrorCode::argument, "Invalid resolved PSF dimensions");
    }
    double sum = 0;
    double cx = 0;
    double cy = 0;
    for (std::uint32_t y = 0; y < psf.height; ++y) {
        for (std::uint32_t x = 0; x < psf.width; ++x) {
            const auto v = std::span<const double>{psf.coefficients}
                               .subspan((std::size_t{y} * psf.width) + x, 1)
                               .front();
            if (!std::isfinite(v) || v < 0) {
                return core::failure(core::ErrorCode::argument, "Invalid resolved PSF coefficient");
            }
            sum += v;
            cx += v * (static_cast<double>(x) - ((static_cast<double>(psf.width) - 1) / 2));
            cy += v * (static_cast<double>(y) - ((static_cast<double>(psf.height) - 1) / 2));
        }
    }
    constexpr double tolerance = 1e-12;
    if (!std::isfinite(sum) || std::abs(sum - 1) > tolerance || !std::isfinite(psf.centroid_x) ||
        !std::isfinite(psf.centroid_y) || std::abs(cx - psf.centroid_x) > tolerance ||
        std::abs(cy - psf.centroid_y) > tolerance) {
        return core::failure(core::ErrorCode::argument,
                             "Invalid resolved PSF normalization or centroid");
    }
    return {};
}
} // namespace docenhance::methods
