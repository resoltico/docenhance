// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/image/numeric.hpp"

#include "docenhance/core/result.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace docenhance::image {
namespace {
// IEC 61966-2-1 sRGB transfer function. Each constant is the published decimal value; derived
// forms (for example 1 + offset) are deliberately not computed, to keep results bit-identical.
constexpr double srgb_encoded_threshold = 0.04045;
constexpr double srgb_linear_threshold = 0.0031308;
constexpr double srgb_linear_slope = 12.92;
constexpr double srgb_offset = 0.055;
constexpr double srgb_scale = 1.055;
constexpr double srgb_exponent = 2.4;
// ITU-R BT.709 relative luminance coefficients (sRGB primaries, D65 white).
constexpr double luminance_red = 0.2126;
constexpr double luminance_green = 0.7152;
constexpr double luminance_blue = 0.0722;
[[nodiscard]] bool unit(double v) {
    return std::isfinite(v) && v >= 0.0 && v <= 1.0;
}
[[nodiscard]] core::Error bad(std::string_view message) {
    return {.code = core::ErrorCode::argument, .message = std::string(message)};
}
} // namespace
core::Result<double> srgb_decode(double encoded) {
    if (!unit(encoded)) {
        return std::unexpected(bad("Encoded sample outside [0,1]"));
    }
    return encoded <= srgb_encoded_threshold
               ? encoded / srgb_linear_slope
               : std::pow((encoded + srgb_offset) / srgb_scale, srgb_exponent);
}
core::Result<double> srgb_encode(double linear) {
    if (!unit(linear)) {
        return std::unexpected(bad("Linear sample outside [0,1]"));
    }
    return linear <= srgb_linear_threshold
               ? (srgb_linear_slope * linear)
               : ((srgb_scale * std::pow(linear, 1.0 / srgb_exponent)) - srgb_offset);
}
core::Result<double> luminance(const Rgb& rgb) {
    if (!std::ranges::all_of(rgb, unit)) {
        return std::unexpected(bad("Invalid RGB sample"));
    }
    return (luminance_red * rgb.at(0)) + (luminance_green * rgb.at(1)) +
           (luminance_blue * rgb.at(2));
}
core::Result<Rgb> transport_luminance(const Rgb& rgb, double target) {
    const auto y = luminance(rgb);
    if (!y) {
        return std::unexpected(y.error());
    }
    if (!unit(target)) {
        return std::unexpected(bad("Target luminance outside [0,1]"));
    }
    Rgb out{};
    if (target <= y.value()) {
        const double ratio = y.value() == 0.0 ? 0.0 : target / y.value();
        for (std::size_t i = 0; i < out.size(); ++i) {
            out.at(i) = rgb.at(i) * ratio;
        }
    } else {
        const double ratio = (target - y.value()) / (1.0 - y.value());
        for (std::size_t i = 0; i < out.size(); ++i) {
            out.at(i) = rgb.at(i) + (ratio * (1.0 - rgb.at(i)));
        }
    }
    // Remove rounding-only excursions. This does not authorize clipping transform errors.
    for (auto& c : out) {
        c = std::clamp(c, 0.0, 1.0);
    }
    return out;
}
core::Result<double> nearest_rank(std::span<const double> values, double p) {
    if (values.empty() || !unit(p) ||
        !std::ranges::all_of(values, [](double v) { return std::isfinite(v); })) {
        return std::unexpected(bad("Invalid percentile input"));
    }
    std::vector<double> work(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(values.size())));
    const auto index = rank == 0 ? 0 : std::min(rank - 1, values.size() - 1);
    std::nth_element(work.begin(), work.begin() + static_cast<std::ptrdiff_t>(index), work.end());
    return work.at(index);
}
std::size_t reflect101_folded(std::int64_t coordinate, std::size_t extent) noexcept {
    if (extent == 1) {
        return 0;
    }
    const auto period = 2 * static_cast<std::int64_t>(extent - 1);
    auto reduced = coordinate % period;
    if (reduced < 0) {
        reduced += period;
    }
    const auto last = static_cast<std::int64_t>(extent - 1);
    return static_cast<std::size_t>(reduced <= last ? reduced : period - reduced);
}
core::Result<std::size_t> reflect101(std::int64_t coordinate, std::size_t extent) {
    if (extent == 0 || extent > max_reflect_extent()) {
        return std::unexpected(bad("Invalid reflection extent"));
    }
    return reflect101_folded(coordinate, extent);
}
core::Result<std::size_t> checked_elements(std::size_t width, std::size_t height,
                                           std::size_t channels, std::size_t limit) {
    if (width == 0 || height == 0 || channels == 0) {
        return std::unexpected(bad("Zero-sized raster"));
    }
    if (width > limit / height) {
        return std::unexpected(bad("Raster exceeds element budget"));
    }
    const auto pixels = width * height;
    if (pixels > limit / channels) {
        return std::unexpected(bad("Raster channels exceed element budget"));
    }
    return pixels * channels;
}
} // namespace docenhance::image
