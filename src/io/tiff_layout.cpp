// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "tiff_layout.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/tiff.hpp"
#include "tiff_ifd.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <tiff.h>
#include <tiffio.h>
namespace docenhance::io {
namespace {
core::Result<TiffLayout> invalid() {
    return core::failure(core::ErrorCode::input, "TIFF strip/tile framing is invalid");
}
bool offsets_type(unsigned type) noexcept {
    return type == TIFF_SHORT || type == TIFF_LONG || type == TIFF_LONG8;
}
bool storage_fields(const TiffIfd& ifd, bool tiled, std::uint64_t width,
                    std::uint64_t height) noexcept {
    if (tiled) {
        return width % image::word_bits == 0 && height % image::word_bits == 0 &&
               !ifd.field(TIFFTAG_STRIPOFFSETS) && !ifd.field(TIFFTAG_STRIPBYTECOUNTS) &&
               !ifd.field(TIFFTAG_ROWSPERSTRIP);
    }
    return !ifd.field(TIFFTAG_TILELENGTH) && !ifd.field(TIFFTAG_TILEOFFSETS) &&
           !ifd.field(TIFFTAG_TILEBYTECOUNTS);
}
core::Result<void> validate_units(const TiffIfd& ifd, const TiffField& offsets,
                                  const TiffField& counts, unsigned units,
                                  const core::Cancellation& cancellation) {
    const auto offset_size = offsets.bytes.size() / static_cast<std::size_t>(units);
    const auto count_size = counts.bytes.size() / static_cast<std::size_t>(units);
    for (std::size_t i = 0; i < units; ++i) {
        if (cancellation.requested(core::Checkpoint::decode)) {
            return std::unexpected(core::cancelled().error());
        }
        const auto offset = ifd.number(offsets.bytes.subspan(i * offset_size, offset_size));
        const auto count = ifd.number(counts.bytes.subspan(i * count_size, count_size));
        if (count > tiff_encoded_unit_max) {
            return core::failure(core::ErrorCode::resource, "TIFF encoded-unit limit exceeded");
        }
        if (count == 0 || offset > ifd.bytes.size() || count > ifd.bytes.size() - offset) {
            return core::failure(core::ErrorCode::input, "TIFF strile extent is invalid");
        }
    }
    return {};
}
} // namespace
core::Result<TiffLayout> tiff_layout(const TiffIfd& ifd, const image::TiffSource& source,
                                     const core::Cancellation& cancellation) {
    const auto width = source.tiled ? ifd.integer(TIFFTAG_TILEWIDTH, 0)
                                    : std::optional<std::uint64_t>{source.width};
    const auto height =
        ifd.integer(source.tiled ? TIFFTAG_TILELENGTH : TIFFTAG_ROWSPERSTRIP, UINT32_MAX);
    const auto offsets = ifd.field(source.tiled ? TIFFTAG_TILEOFFSETS : TIFFTAG_STRIPOFFSETS);
    const auto counts = ifd.field(source.tiled ? TIFFTAG_TILEBYTECOUNTS : TIFFTAG_STRIPBYTECOUNTS);
    if (!width || !height || *width == 0 || *height == 0 || *width > UINT32_MAX ||
        *height > UINT32_MAX || !offsets || !counts || !offsets_type(offsets->type) ||
        !offsets_type(counts->type) || !storage_fields(ifd, source.tiled, *width, *height) ||
        (ifd.integer(TIFFTAG_IMAGEDEPTH, 1) != 1) || (ifd.integer(TIFFTAG_TILEDEPTH, 1) != 1)) {
        return invalid();
    }
    const auto unit_height =
        source.tiled ? *height : std::min(*height, std::uint64_t{source.height});
    const auto across = (std::uint64_t{source.width} + *width - 1) / *width;
    const auto down = (std::uint64_t{source.height} + unit_height - 1) / unit_height;
    const unsigned planes = source.planar == 2 ? source.samples : 1;
    const auto units = across * down * planes;
    const auto components = source.planar == 2 ? 1 : source.samples;
    const auto row_bytes =
        ((*width * components * source.depth) + (image::byte_bits - 1)) / image::byte_bits;
    if (row_bytes == 0 || row_bytes > tiff_decoded_unit_max ||
        unit_height > tiff_decoded_unit_max / row_bytes || units > tiff_units_max) {
        return core::failure(core::ErrorCode::resource,
                             "TIFF decoded-unit or strile-count limit exceeded");
    }
    if (offsets->count != units || counts->count != units) {
        return invalid();
    }
    const auto bounded =
        validate_units(ifd, *offsets, *counts, static_cast<unsigned>(units), cancellation);
    if (!bounded) {
        return std::unexpected(bounded.error());
    }
    if (source.photometric == image::tiff_palette) {
        const auto palette = ifd.field(TIFFTAG_COLORMAP);
        constexpr unsigned palette_entries = image::tiff_color_samples * (image::byte_max + 1);
        if (!palette || palette->type != TIFF_SHORT || palette->count != palette_entries) {
            return core::failure(core::ErrorCode::input,
                                 "TIFF palette requires 256 complete 16-bit RGB entries");
        }
    }
    return TiffLayout{
        .width = static_cast<std::uint32_t>(*width),
        .height = static_cast<std::uint32_t>(unit_height),
        .across = static_cast<std::uint32_t>(across),
        .down = static_cast<std::uint32_t>(down),
        .planes = planes,
        .row_bytes = static_cast<std::size_t>(row_bytes),
        .unit_bytes = static_cast<std::size_t>(row_bytes * unit_height),
        .units = static_cast<unsigned>(units),
    };
}
} // namespace docenhance::io
