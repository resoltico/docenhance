// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/source.hpp"

#include "docenhance/image/raster.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
namespace docenhance::image {
namespace {
bool dimensions(std::uint32_t width, std::uint32_t height) noexcept {
    return width != 0 && height != 0 && std::uint64_t{width} * height <= source_pixels_max;
}
bool resolution(const std::optional<Resolution>& value) noexcept {
    return !value || (value->x != 0 && value->y != 0);
}
bool png_description(const PngSource& png) noexcept {
    if (!dimensions(png.width, png.height)) {
        return false;
    }
    const auto depth = png.depth;
    const bool short_depth =
        depth == 1 || depth == 2 || depth == png_nibble_depth || depth == byte_bits;
    if (png.color_type == 0) {
        return short_depth || depth == word_bits;
    }
    if (png.color_type == png_palette) {
        return short_depth;
    }
    const bool direct =
        png.color_type == 2 || png.color_type == png_gray_alpha || png.color_type == png_rgb_alpha;
    return direct && (depth == byte_bits || depth == word_bits);
}
bool jpeg_metadata(const JpegSource& jpeg) noexcept {
    if (!resolution(jpeg.jfif_resolution) || !resolution(jpeg.exif_resolution) ||
        (!jpeg.jfif_present && jpeg.jfif_resolution) ||
        (!jpeg.exif_present && jpeg.exif_resolution)) {
        return false;
    }
    if (jpeg.adobe_transform && *jpeg.adobe_transform > 1) {
        return false;
    }
    if (jpeg.jfif_present && jpeg.color == JpegColor::rgb) {
        return false;
    }
    if (jpeg.adobe_transform) {
        const bool ycc = jpeg.color == JpegColor::ycbcr;
        if ((*jpeg.adobe_transform == 1) != ycc) {
            return false;
        }
    }
    const bool conflict = jpeg.exif_resolution && jpeg.jfif_resolution &&
                          *jpeg.exif_resolution != *jpeg.jfif_resolution;
    return conflict == jpeg.resolution_conflict;
}
bool jpeg_sampling(const JpegSource& jpeg, unsigned components) noexcept {
    const auto first = jpeg.sampling.front();
    if ((first.horizontal * first.vertical) + components - 1 > jpeg_mcu_blocks_max) {
        return false;
    }
    for (const auto sample : std::span{jpeg.sampling}) {
        if (sample.horizontal == 0 || sample.horizontal > jpeg_sampling_max ||
            sample.vertical == 0 || sample.vertical > jpeg_sampling_max) {
            return false;
        }
    }
    for (unsigned i = 0; i < jpeg.sampling.size(); ++i) {
        const auto sample = std::span{jpeg.sampling}.subspan(i, 1).front();
        if ((i != 0 || jpeg.color != JpegColor::ycbcr) &&
            (sample.horizontal != 1 || sample.vertical != 1)) {
            return false;
        }
    }
    return true;
}
bool jpeg_identity(const JpegSource& jpeg, unsigned components) noexcept {
    for (unsigned i = 0; i < components; ++i) {
        const auto identity = std::span{jpeg.component_ids}.subspan(i, 1).front();
        if (identity > byte_max) {
            return false;
        }
        for (unsigned prior = 0; prior < i; ++prior) {
            if (identity == std::span{jpeg.component_ids}.subspan(prior, 1).front()) {
                return false;
            }
        }
    }
    if (components == 1 || jpeg.jfif_present || jpeg.adobe_transform) {
        return true;
    }
    constexpr auto rgb = std::to_array<unsigned>({'R', 'G', 'B'});
    constexpr auto ycc = std::to_array<unsigned>({1, 2, jpeg_components});
    return jpeg.component_ids == (jpeg.color == JpegColor::rgb ? rgb : ycc);
}
bool jpeg_description(const JpegSource& jpeg) noexcept {
    const bool known_color = jpeg.color == JpegColor::gray || jpeg.color == JpegColor::rgb ||
                             jpeg.color == JpegColor::ycbcr;
    const bool known_process =
        jpeg.process == JpegProcess::baseline || jpeg.process == JpegProcess::progressive;
    if (!known_color || !known_process) {
        return false;
    }
    const bool gray = jpeg.color == JpegColor::gray;
    const unsigned components = gray ? 1 : jpeg_components;
    if (!dimensions(jpeg.width, jpeg.height) || jpeg.width > jpeg_dimension_max ||
        jpeg.height > jpeg_dimension_max || jpeg.scans == 0 || jpeg.scans > jpeg_scan_max) {
        return false;
    }
    if (jpeg.process == JpegProcess::baseline && jpeg.scans > components) {
        return false;
    }
    return jpeg_metadata(jpeg) && jpeg_sampling(jpeg, components) &&
           jpeg_identity(jpeg, components);
}
} // namespace
bool valid_source_description(const SourceDescription& description) noexcept {
    if (const auto* const png = std::get_if<PngSource>(&description)) {
        return png_description(*png);
    }
    const auto* const jpeg = std::get_if<JpegSource>(&description);
    return jpeg != nullptr && jpeg_description(*jpeg);
}
} // namespace docenhance::image
