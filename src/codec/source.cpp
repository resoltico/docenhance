// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/source.hpp"

#include "docenhance/codec/continuous_png.hpp"
#include "docenhance/codec/jpeg.hpp"
#include "docenhance/codec/tiff.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/source_facts.hpp"
#include "encoded_bytes.hpp"
#include "jpeg_markers.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::codec {
namespace {
constexpr std::size_t png_depth_offset = 24;
constexpr std::size_t png_color_offset = 25;
constexpr std::size_t png_interlace_offset = 28;
} // namespace
core::Result<DecodedRaster> decode_source(std::span<const std::byte> input, core::Budget& budget,
                                          image::ProfilePolicy policy,
                                          const core::Cancellation& cancellation) {
    const auto bytes = encoded_bytes(input);
    if (bytes.size() >= 2 && bytes.front() == jpeg_marker_prefix &&
        bytes.subspan(1, 1).front() == jpeg_soi) {
        auto decoded = decode_jpeg(bytes, budget, policy, cancellation);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        return DecodedRaster{
            .raster = std::move(decoded->raster),
            .description = decoded->source,
        };
    }
    if (has_tiff_signature(bytes)) {
        auto decoded = decode_tiff(bytes, budget, policy, cancellation);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        return DecodedRaster{
            .raster = std::move(decoded->raster),
            .description = decoded->source,
        };
    }
    constexpr auto png_signature = std::to_array<std::uint8_t>({137, 80, 78, 71, 13, 10, 26, 10});
    if (bytes.size() < png_signature.size() ||
        !std::ranges::equal(png_signature, bytes.first(png_signature.size()))) {
        return core::failure(core::ErrorCode::input,
                             "Input signature is not an admitted PNG, JPEG or TIFF source");
    }
    auto raster = decode_png_raster(bytes, budget, policy, cancellation);
    if (!raster) {
        return std::unexpected(raster.error());
    }
    // The PNG decoder has already established the complete IHDR and container framing.
    const image::PngFacts description{
        .width = raster->shape.width,
        .height = raster->shape.height,
        .depth = bytes.subspan(png_depth_offset, 1).front(),
        .color_type = bytes.subspan(png_color_offset, 1).front(),
        .interlaced = bytes.subspan(png_interlace_offset, 1).front() != 0,
    };
    return DecodedRaster{
        .raster = std::move(*raster),
        .description = description,
    };
}
} // namespace docenhance::codec
