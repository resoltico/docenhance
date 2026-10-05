// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/io/png_artifact.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "png_metadata.hpp"
#include "png_rows.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <utility>
namespace docenhance::io {
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
           (depth == image::byte_bits || depth == image::word_bits) &&
           (color == 0 || color == rgb_type);
}
bool canonical_chunks(std::span<const std::uint8_t> bytes, const core::Cancellation& cancellation,
                      bool& stopped) {
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
} // namespace
core::Result<PngArtifact> observe_png_artifact(std::span<const std::byte> bytes,
                                               core::Budget& budget,
                                               const core::Cancellation& cancellation) {
    // Bytes are a borrowed view of the retained immutable encoded snapshot.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* const data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    const std::span encoded{data, bytes.size()};
    bool stopped = false;
    if (!canonical_chunks(encoded, cancellation, stopped)) {
        if (stopped) {
            return core::cancelled();
        }
        return core::failure(core::ErrorCode::input,
                             "Bundle PNG has unsupported encoding or metadata");
    }
    auto raster = decode_result_png_raster(encoded, budget, cancellation);
    if (!raster) {
        return std::unexpected(raster.error());
    }
    PngArtifact result{
        .shape = raster->shape,
        .profile_embedded = !raster->metadata.icc.empty(),
        .profile = std::move(raster->metadata.icc),
        .resolution = raster->metadata.resolution,
    };
    constexpr std::size_t poll = 4096;
    for (std::uint32_t y = 0; y < raster->shape.height; ++y) {
        const auto row = raster->pixels.view().row(y);
        for (std::size_t x = 0; x < row.size(); ++x) {
            if (x % poll == 0 && cancellation.requested(core::Checkpoint::verification)) {
                return core::cancelled();
            }
            const auto value = row.subspan(x, 1).front();
            result.binary_samples =
                result.binary_samples && (value == 0 || value == image::byte_max);
            result.mask_samples = result.mask_samples && (value == 0 || value == 1);
            result.protected_samples += static_cast<std::uint64_t>(value != 0);
        }
    }
    return result;
}
} // namespace docenhance::io
