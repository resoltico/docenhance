// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/psf_png.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "encoded_bytes.hpp"
#include "png_metadata.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
namespace docenhance::codec {
namespace {
core::Result<void> psf_header(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t width_offset = 16;
    constexpr std::size_t height_offset = 20;
    constexpr std::size_t depth_offset = 24;
    constexpr std::size_t color_offset = 25;
    constexpr std::uint32_t minimum_dimension = 3;
    if (bytes.size() <= color_offset) {
        return core::failure(core::ErrorCode::input,
                             "PSF requires a complete grayscale PNG header");
    }
    const auto width = png_integer(bytes.subspan(width_offset));
    const auto height = png_integer(bytes.subspan(height_offset));
    const auto depth = bytes.subspan(depth_offset, 1).front();
    if (width < minimum_dimension || height < minimum_dimension || width > psf_dimension_max ||
        height > psf_dimension_max || width % 2 == 0 || height % 2 == 0 ||
        (depth != image::byte_bits && depth != image::word_bits) ||
        bytes.subspan(color_offset, 1).front() != 0) {
        return core::failure(core::ErrorCode::input,
                             "PSF requires raw 8/16-bit gray PNG with odd dimensions in [3,129]");
    }
    return {};
}
} // namespace
core::Result<image::Raster> decode_psf_png(std::span<const std::byte> input, core::Budget& budget,
                                           const core::Cancellation& cancellation) {
    const auto bytes = encoded_bytes(input);
    const auto admitted = psf_header(bytes);
    if (!admitted) {
        return std::unexpected(admitted.error());
    }
    auto decoded =
        decode_stored_png_raster(bytes, budget,
                                 {
                                     .encoded_bytes = psf_encoded_bytes_max,
                                     .pixels = std::uint64_t{psf_dimension_max} * psf_dimension_max,
                                 },
                                 cancellation);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    if (decoded->shape.model != image::SampleModel::gray) {
        return core::failure(core::ErrorCode::input, "PSF coefficients cannot have transparency");
    }
    return decoded;
}
} // namespace docenhance::codec
