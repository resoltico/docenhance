// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/digest.hpp"
#include "docenhance/io/png.hpp"
#include "jpeg_markers.hpp"
#include "source_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
namespace docenhance::io {
namespace {
constexpr std::size_t png_depth_offset = 24;
constexpr std::size_t png_interlace_offset = 28;
} // namespace
core::Result<IdentifiedImage> load_grayscale_png(const std::string& input, core::Budget& budget,
                                                 const core::Cancellation& cancellation) {
    // Snapshot first: an identity must describe the bytes this decode consumed. Streaming the
    // file and hashing a later reopening of the same path can answer about two different files.
    auto encoded = read_source_snapshot(input, budget, cancellation);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    auto source = identify(encoded->bytes(), cancellation);
    if (!source) {
        return std::unexpected(source.error());
    }
    // uint8_t is the unsigned-byte view of the immutable encoded snapshot.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* const data = reinterpret_cast<const std::uint8_t*>(encoded->bytes().data());
    if (encoded->size() >= 2 && std::span{data, encoded->size()}.front() == jpeg_marker_prefix &&
        std::span{data, encoded->size()}.subspan(1, 1).front() == jpeg_soi) {
        return core::failure(
            core::ErrorCode::unavailable,
            "JPEG input supports continuous output only; binary processing requires grayscale PNG");
    }
    auto decoded = decode_grayscale_png({data, encoded->size()}, budget, PngLimits(), cancellation);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    const std::span bytes{data, encoded->size()};
    const image::PngSource description{
        .width = decoded->width(),
        .height = decoded->height(),
        .depth = bytes.subspan(png_depth_offset, 1).front(),
        .color_type = 0,
        .interlaced = bytes.subspan(png_interlace_offset, 1).front() != 0,
    };
    return IdentifiedImage{
        .image = std::move(*decoded),
        .source = std::move(*source),
        .description = description,
    };
}
} // namespace docenhance::io
