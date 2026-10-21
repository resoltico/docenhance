// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/png_artifact.hpp"

#include "docenhance/codec/png.hpp"
#include "docenhance/codec/png_output.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "encoded_bytes.hpp"
#include "png_metadata.hpp"
#include "png_rows.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <png.h>
#include <span>
#include <string_view>
#include <utility>
namespace docenhance::codec {
namespace {
bool canonical_metadata(std::uint32_t type, std::span<const std::uint8_t> data) {
    constexpr std::uint32_t iccp = 0x69434350;
    constexpr std::uint32_t phys = 0x70485973;
    constexpr std::size_t resolution_bytes = 9;
    if (type == phys) {
        return data.size() == resolution_bytes && data.back() == 1 && png_integer(data) != 0 &&
               png_integer(data.subspan(png_integer_bytes)) != 0;
    }
    if (type != iccp) {
        return true;
    }
    const std::string_view name{output_profile_name};
    if (data.size() < name.size() + 2 || data.subspan(name.size(), 1).front() != 0 ||
        data.subspan(name.size() + 1, 1).front() != 0) {
        return false;
    }
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (data.subspan(i, 1).front() != static_cast<std::uint8_t>(name.at(i))) {
            return false;
        }
    }
    return true;
}
bool canonical_header(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t depth_offset = 24;
    constexpr std::size_t color_offset = 25;
    constexpr std::size_t interlace_offset = 28;
    constexpr unsigned rgb_type = 2;
    if (bytes.size() <= interlace_offset) {
        return false;
    }
    const auto depth = bytes.subspan(depth_offset, 1).front();
    const auto color = bytes.subspan(color_offset, 1).front();
    return bytes.subspan(interlace_offset, 1).front() == 0 &&
           (depth == image::byte_bits || depth == image::word_bits || (depth == 1 && color == 0)) &&
           (color == 0 || color == rgb_type);
}
bool agrees(const PngArtifact& artifact, const PngImage& intended) noexcept {
    const auto shape = artifact.shape;
    const auto row_bytes = std::uint64_t{shape.width} * image::components(shape.model) *
                           (shape.depth.bits() == 1 ? 1 : shape.depth.bits() / image::byte_bits);
    return shape == intended.shape && intended.pixels.width() == row_bytes &&
           intended.pixels.height() == shape.height && artifact.resolution == intended.resolution &&
           std::ranges::equal(artifact.profile.bytes(), intended.profile);
}
} // namespace
bool canonical_png_chunks(std::span<const std::uint8_t> bytes,
                          const core::Cancellation& cancellation, bool& stopped) {
    constexpr std::uint32_t iccp = 0x69434350;
    constexpr std::uint32_t phys = 0x70485973;
    constexpr std::size_t header_data = 13;
    if (!canonical_header(bytes)) {
        return false;
    }
    auto remaining = bytes.subspan(png_signature_bytes);
    std::size_t chunks = 0;
    while (remaining.size() >= png_chunk_overhead) {
        if (chunks == png_max_chunks) {
            return false;
        }
        ++chunks;
        if (cancellation.requested(core::Checkpoint::verification)) {
            stopped = true;
            return false;
        }
        const auto length = png_integer(remaining);
        const auto type = png_integer(remaining.subspan(png_integer_bytes));
        const bool allowed = type == chunk_ihdr || type == chunk_idat || type == chunk_iend ||
                             type == iccp || type == phys;
        if (!allowed || length > remaining.size() - png_chunk_overhead ||
            (type == chunk_ihdr && length != header_data)) {
            return false;
        }
        if (!canonical_metadata(type, remaining.subspan(png_signature_bytes, length))) {
            return false;
        }
        remaining = remaining.subspan(std::size_t{length} + png_chunk_overhead);
    }
    return remaining.empty();
}
core::Result<PngArtifact> describe_canonical_png(std::span<const std::byte> bytes,
                                                 const PngImage* const intended,
                                                 core::Budget& budget,
                                                 const core::Cancellation& cancellation) {
    const auto encoded = encoded_bytes(bytes);
    auto scanned = scan_png(
        encoded, budget,
        {
            .limits = {.encoded_bytes = core::bundle_max_file_bytes, .pixels = png_max_pixels},
            .content = PngContent::result,
        },
        cancellation);
    if (!scanned) {
        return std::unexpected(std::move(scanned.error()));
    }
    bool stopped = false;
    if (!canonical_png_chunks(encoded, cancellation, stopped)) {
        if (stopped) {
            return core::cancelled();
        }
        return core::failure(core::ErrorCode::input, "Noncanonical PNG encoding or metadata");
    }
    constexpr std::size_t depth_offset = 24;
    const auto depth = image::PngDepth::from_bits(encoded.subspan(depth_offset, 1).front());
    if (!depth) {
        return core::failure(core::ErrorCode::input, "Noncanonical PNG encoding or metadata");
    }
    PngArtifact artifact{
        .shape =
            {
                .width = scanned->shape.width,
                .height = scanned->shape.height,
                .model = encoded.subspan(depth_offset + 1, 1).front() == PNG_COLOR_TYPE_GRAY
                             ? image::SampleModel::gray
                             : image::SampleModel::rgb,
                .depth = *depth,
            },
        .profile_embedded = !scanned->metadata.icc.empty(),
        .profile = std::move(scanned->metadata.icc),
        .resolution = scanned->metadata.resolution,
        .binary_samples = encoded.subspan(depth_offset, 1).front() == 1,
    };
    const auto shape = artifact.shape;
    const bool mask = artifact.profile.empty() && !artifact.binary_samples;
    if ((artifact.binary_samples && !artifact.profile.empty()) ||
        (mask && (shape.model != image::SampleModel::gray ||
                  shape.depth.bits() != image::byte_bits || artifact.resolution)) ||
        (intended != nullptr && !agrees(artifact, *intended))) {
        return core::failure(core::ErrorCode::input,
                             "PNG description differs from intended output");
    }
    return artifact;
}
core::Result<PngArtifact> observe_png_artifact(std::span<const std::byte> bytes,
                                               core::Budget& budget,
                                               const core::Cancellation& cancellation) {
    return read_canonical_png(bytes, nullptr, budget, cancellation);
}
} // namespace docenhance::codec
