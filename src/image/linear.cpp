// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/image/linear.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace docenhance::image {
core::Result<void> quantize_linear(RasterShape output, std::span<const double> rgb,
                                   std::span<std::uint8_t> bytes) {
    const unsigned channels = components(output.model);
    if ((output.model != SampleModel::gray && output.model != SampleModel::rgb) ||
        (output.depth != byte_bits && output.depth != word_bits) || rgb.empty() ||
        rgb.size() % rgb_channels != 0 || rgb.size() / rgb_channels > linear_block_pixels) {
        return core::failure(core::ErrorCode::argument, "Invalid linear quantization block");
    }
    const auto sample_bytes = output.depth / byte_bits;
    if (bytes.size() != (rgb.size() / rgb_channels) * channels * sample_bytes) {
        return core::failure(core::ErrorCode::argument, "Quantization output extent mismatch");
    }
    const auto maximum = output.depth == word_bits ? word_max : byte_max;
    for (std::size_t i = 0; i < rgb.size() / rgb_channels; ++i) {
        const auto pixel = rgb.subspan(i * rgb_channels, rgb_channels);
        Rgb values{pixel.front(), pixel.subspan(1).front(), pixel.subspan(2).front()};
        const auto y = luminance(values);
        if (!y) {
            return std::unexpected(y.error());
        }
        if (channels == 1) {
            values.front() = *y;
        }
        for (unsigned c = 0; c < channels; ++c) {
            const auto encoded = srgb_encode(values.at(c));
            if (!encoded) {
                return std::unexpected(encoded.error());
            }
            const auto sample = static_cast<std::uint16_t>(
                std::floor((std::clamp(*encoded, 0.0, 1.0) * maximum) + 0.5));
            write_sample(bytes.subspan(((i * channels) + c) * sample_bytes, sample_bytes),
                         output.depth, sample);
        }
    }
    return {};
}
} // namespace docenhance::image
