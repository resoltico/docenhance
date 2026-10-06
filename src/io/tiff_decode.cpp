// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/tiff.hpp"
#include "jpeg_context.hpp"
#include "tiff_context.hpp"
#include "tiff_ifd.hpp"
#include "tiff_layout.hpp"

#include <cstdint>
#include <docenhance_tiff.h>
#include <expected>
#include <span>
#include <tiff.h>
#include <tiffio.h>
#include <utility>
namespace docenhance::io {
namespace {
bool agrees(const TiffContext& context, const image::TiffSource& source) {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint16_t bits = 0;
    std::uint16_t samples = 0;
    std::uint16_t photo = 0;
    std::uint16_t compression = 0;
    std::uint16_t planar = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    TIFFGetField(context.decoder, TIFFTAG_IMAGEWIDTH, &width);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    TIFFGetField(context.decoder, TIFFTAG_IMAGELENGTH, &height);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    TIFFGetFieldDefaulted(context.decoder, TIFFTAG_BITSPERSAMPLE, &bits);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    TIFFGetFieldDefaulted(context.decoder, TIFFTAG_SAMPLESPERPIXEL, &samples);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    TIFFGetField(context.decoder, TIFFTAG_PHOTOMETRIC, &photo);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    TIFFGetFieldDefaulted(context.decoder, TIFFTAG_COMPRESSION, &compression);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    TIFFGetFieldDefaulted(context.decoder, TIFFTAG_PLANARCONFIG, &planar);
    const auto units =
        source.tiled ? TIFFNumberOfTiles(context.decoder) : TIFFNumberOfStrips(context.decoder);
    // libtiff's fixed tag-specific variadic ABI consumes this promoted int color-mode value.
    const bool color_mode =
        source.photometric != image::tiff_ycbcr ||
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        TIFFSetField(context.decoder, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB) == 1;
    return color_mode && context.good() && width == source.width && height == source.height &&
           bits == source.depth && samples == source.samples && photo == source.photometric &&
           compression == source.compression && planar == source.planar &&
           units == context.layout.units && (TIFFIsTiled(context.decoder) != 0) == source.tiled;
}
core::Result<image::Raster> prepared_raster(const TiffContext& context,
                                            const image::TiffSource& source, core::Budget& budget,
                                            image::ProfilePolicy profile) {
    auto metadata = tiff_metadata(context.ifd, source, budget, profile, context.cancellation.get());
    if (!metadata) {
        return std::unexpected(metadata.error());
    }
    const auto raster_shape = image::decoded_tiff_shape(source);
    auto row = image::raster_row_bytes(raster_shape);
    if (!row) {
        return std::unexpected(row.error());
    }
    if (context.cancellation.get().requested(core::Checkpoint::allocation)) {
        return std::unexpected(core::cancelled().error());
    }
    auto pixels = image::Plane<std::uint8_t>::allocate(budget, *row, source.height);
    if (!pixels) {
        return std::unexpected(pixels.error());
    }
    image::Raster raster{
        .shape = raster_shape,
        .pixels = std::move(*pixels),
        .metadata = std::move(*metadata),
    };
    return raster;
}
} // namespace
core::Result<DecodedTiff> decode_tiff(std::span<const std::uint8_t> bytes, core::Budget& budget,
                                      image::ProfilePolicy profile,
                                      const core::Cancellation& cancellation, TiffLimits limits) {
    if (profile != image::ProfilePolicy::embedded && profile != image::ProfilePolicy::srgb) {
        return core::failure(core::ErrorCode::argument,
                             "Unknown TIFF profile interpretation policy");
    }
    if (limits.encoded_bytes == 0 || limits.encoded_bytes > image::source_encoded_bytes_max ||
        limits.pixels == 0 || limits.pixels > image::source_pixels_max) {
        return core::failure(core::ErrorCode::argument, "TIFF limits exceed the production domain");
    }
    if (bytes.size() > limits.encoded_bytes) {
        return core::failure(core::ErrorCode::resource, "TIFF encoded-byte limit exceeded");
    }
    auto ifd = scan_tiff(bytes, cancellation);
    if (!ifd) {
        return std::unexpected(ifd.error());
    }
    auto source = tiff_description(*ifd);
    if (!source) {
        return std::unexpected(source.error());
    }
    if (std::uint64_t{source->width} * source->height > limits.pixels) {
        return core::failure(core::ErrorCode::resource, "TIFF pixel limit exceeded");
    }
    auto layout = tiff_layout(*ifd, *source, cancellation);
    if (!layout) {
        return std::unexpected(layout.error());
    }
    if (cancellation.requested(core::Checkpoint::allocation)) {
        return std::unexpected(core::cancelled().error());
    }
    auto native = budget.reserve(tiff_native_payload_max + tiff_control_reservation);
    if (!native) {
        return std::unexpected(native.error());
    }
    auto bootstrap =
        budget.reserve(source->compression == image::tiff_jpeg ? jpeg_bootstrap_bytes : 0);
    if (!bootstrap) {
        return std::unexpected(bootstrap.error());
    }
    DocEnhanceTiffJpegControl installer{};
    TiffContext context{budget, cancellation};
    context.bytes = bytes;
    context.ifd = *ifd;
    context.layout = *layout;
    if (!open_tiff(context)) {
        return std::unexpected(context.error());
    }
    bind_tiff_jpeg(context, installer);
    if (!agrees(context, *source)) {
        if (!context.good()) {
            return std::unexpected(context.error());
        }
        return core::failure(core::ErrorCode::input,
                             "TIFF native header differs from admitted framing");
    }
    auto raster = prepared_raster(context, *source, budget, profile);
    if (!raster) {
        return std::unexpected(raster.error());
    }
    const auto decoded = tiff_pixels(context, *source, *raster, budget);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    return DecodedTiff{.raster = std::move(*raster), .source = *source};
}
} // namespace docenhance::io
