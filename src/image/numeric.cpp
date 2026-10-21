// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/numeric.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
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
[[nodiscard]] bool unit(double v) noexcept {
    return std::isfinite(v) && v >= 0.0 && v <= 1.0;
}
[[nodiscard]] core::Error bad(std::string_view message) {
    return {.code = core::ErrorCode::argument, .message = std::string(message)};
}
} // namespace
bool valid_unit_samples(std::span<const double> samples) noexcept {
    return std::ranges::all_of(samples, unit);
}
double srgb_decode(double encoded) noexcept {
    return encoded <= srgb_encoded_threshold
               ? encoded / srgb_linear_slope
               : std::pow((encoded + srgb_offset) / srgb_scale, srgb_exponent);
}
double srgb_encode(double linear) noexcept {
    return linear <= srgb_linear_threshold
               ? (srgb_linear_slope * linear)
               : ((srgb_scale * std::pow(linear, 1.0 / srgb_exponent)) - srgb_offset);
}
double luminance(const Rgb& rgb) noexcept {
    return (luminance_red * std::get<0>(rgb)) + (luminance_green * std::get<1>(rgb)) +
           (luminance_blue * std::get<2>(rgb));
}
Rgb transport_luminance(const Rgb& rgb, double target) noexcept {
    const auto y = luminance(rgb);
    Rgb out{};
    if (target <= y) {
        const double ratio = y == 0.0 ? 0.0 : target / y;
        std::ranges::transform(rgb, out.begin(),
                               [ratio](double component) { return component * ratio; });
    } else {
        const double ratio = (target - y) / (1.0 - y);
        std::ranges::transform(rgb, out.begin(), [ratio](double component) {
            return component + (ratio * (1.0 - component));
        });
    }
    // Remove rounding-only excursions. This does not authorize clipping transform errors.
    for (auto& c : out) {
        c = std::clamp(c, 0.0, 1.0);
    }
    return out;
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
core::Result<void> quantize_linear(RasterShape output, std::span<const double> rgb,
                                   std::span<std::uint8_t> bytes) {
    const unsigned channels = components(output.model);
    if ((output.model != SampleModel::gray && output.model != SampleModel::rgb) || rgb.empty() ||
        rgb.size() % rgb_channels != 0 || rgb.size() / rgb_channels > linear_block_pixels) {
        return core::failure(core::ErrorCode::argument, "Invalid linear quantization block");
    }
    const auto sample_bytes = output.depth.bytes();
    if (bytes.size() != (rgb.size() / rgb_channels) * channels * sample_bytes) {
        return core::failure(core::ErrorCode::argument, "Quantization output extent mismatch");
    }
    if (!valid_unit_samples(rgb)) {
        return core::failure(core::ErrorCode::argument, "Invalid RGB sample");
    }
    const auto maximum = output.depth == SampleDepth::word() ? word_max : byte_max;
    for (std::size_t i = 0; i < rgb.size() / rgb_channels; ++i) {
        const auto pixel = rgb.subspan(i * rgb_channels, rgb_channels);
        Rgb values{pixel.front(), pixel.subspan(1).front(), pixel.subspan(2).front()};
        const auto y = luminance(values);
        if (channels == 1) {
            values.front() = y;
        }
        for (unsigned c = 0; c < channels; ++c) {
            const auto encoded = srgb_encode(values.at(c));
            const auto sample = static_cast<std::uint16_t>(
                std::floor((std::clamp(encoded, 0.0, 1.0) * maximum) + 0.5));
            write_sample(bytes.subspan(((i * channels) + c) * sample_bytes, sample_bytes),
                         output.depth, sample);
        }
    }
    return {};
}
} // namespace docenhance::image
