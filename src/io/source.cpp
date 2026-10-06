// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/io/source.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "docenhance/io/digest.hpp"
#include "docenhance/io/jpeg.hpp"
#include "docenhance/io/tiff.hpp"
#include "jpeg_markers.hpp"
#include "source_snapshot.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
namespace docenhance::io {
namespace {
constexpr std::size_t png_depth_offset = 24;
constexpr std::size_t png_color_offset = 25;
constexpr std::size_t png_interlace_offset = 28;
} // namespace
core::Result<IdentifiedRaster> load_source(const std::string& input, core::Budget& budget,
                                           image::ProfilePolicy policy,
                                           const core::Cancellation& cancellation) {
    auto encoded = read_source_snapshot(input, budget, cancellation);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    auto identity = identify(encoded->bytes(), cancellation);
    if (!identity) {
        return std::unexpected(identity.error());
    }
    // uint8_t views immutable encoded bytes; the snapshot outlives hashing and decoding.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* const data = reinterpret_cast<const std::uint8_t*>(encoded->bytes().data());
    const std::span bytes{data, encoded->size()};
    if (bytes.size() >= 2 && bytes.front() == jpeg_marker_prefix &&
        bytes.subspan(1, 1).front() == jpeg_soi) {
        auto decoded = decode_jpeg(bytes, budget, policy, cancellation);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        return IdentifiedRaster{
            .raster = std::move(decoded->raster),
            .source = std::move(*identity),
            .description = decoded->source,
        };
    }
    if (has_tiff_signature(bytes)) {
        auto decoded = decode_tiff(bytes, budget, policy, cancellation);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        return IdentifiedRaster{
            .raster = std::move(decoded->raster),
            .source = std::move(*identity),
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
    const image::PngSource description{
        .width = raster->shape.width,
        .height = raster->shape.height,
        .depth = bytes.subspan(png_depth_offset, 1).front(),
        .color_type = bytes.subspan(png_color_offset, 1).front(),
        .interlaced = bytes.subspan(png_interlace_offset, 1).front() != 0,
    };
    return IdentifiedRaster{
        .raster = std::move(*raster),
        .source = std::move(*identity),
        .description = description,
    };
}
} // namespace docenhance::io
