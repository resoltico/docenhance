// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/jpeg.hpp"
#include "jpeg_context.hpp"
#include "jpeg_scan.hpp"

#include <csetjmp>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::io {
core::Result<DecodedJpeg> decode_jpeg(std::span<const std::uint8_t> bytes, core::Budget& budget,
                                      image::ProfilePolicy profile,
                                      const core::Cancellation& cancellation, JpegLimits limits) {
    if (profile != image::ProfilePolicy::embedded && profile != image::ProfilePolicy::srgb) {
        return core::failure(core::ErrorCode::argument,
                             "Unknown JPEG profile interpretation policy");
    }
    auto scanned = scan_jpeg(bytes, budget, profile, cancellation, limits);
    if (!scanned) {
        return std::unexpected(scanned.error());
    }
    // The pinned library's creation path allocates only its manager and permanent controllers.
    // This conservative reservation precedes jpeg_create; all subsequent blocks use our allocator.
    auto bootstrap = budget.reserve(jpeg_bootstrap_bytes);
    if (!bootstrap) {
        return std::unexpected(bootstrap.error());
    }
    std::jmp_buf native_jump{};
    JpegContext context{budget, cancellation, native_jump};
    context.scan_limit = limits.scans;
    context.working_charge_peak = budget.used();
    if (!jpeg_header(context, bytes)) {
        return std::unexpected(context.error());
    }
    const auto& observed = scanned->source;
    J_COLOR_SPACE expected_color = JCS_YCbCr;
    if (observed.color == image::JpegColor::gray) {
        expected_color = JCS_GRAYSCALE;
    }
    if (observed.color == image::JpegColor::rgb) {
        expected_color = JCS_RGB;
    }
    if (context.decoder.image_width != observed.width ||
        context.decoder.image_height != observed.height ||
        std::cmp_not_equal(context.decoder.data_precision, image::byte_bits) ||
        context.decoder.jpeg_color_space != expected_color ||
        (context.decoder.progressive_mode != FALSE) !=
            (observed.process == image::JpegProcess::progressive)) {
        return core::failure(core::ErrorCode::input,
                             "JPEG native header differs from admitted declarations");
    }
    const image::RasterShape shape{
        .width = observed.width,
        .height = observed.height,
        .model = observed.color == image::JpegColor::gray ? image::SampleModel::gray
                                                          : image::SampleModel::rgb,
        .depth = image::byte_bits,
    };
    auto row = image::raster_row_bytes(shape);
    if (!row) {
        return std::unexpected(row.error());
    }
    auto pixels = image::Plane<std::uint8_t>::allocate(budget, *row, shape.height);
    if (!pixels) {
        return std::unexpected(pixels.error());
    }
    if (!jpeg_pixels(context, pixels->view())) {
        return std::unexpected(context.error());
    }
    if (std::cmp_not_equal(context.decoder.input_scan_number, observed.scans)) {
        return core::failure(core::ErrorCode::input,
                             "JPEG native scan count differs from admitted framing");
    }
    return DecodedJpeg{
        .raster =
            {
                .shape = shape,
                .pixels = std::move(*pixels),
                .metadata = std::move(scanned->metadata),
            },
        .source = observed,
        .decoder_charge_peak = context.peak_bytes,
        .native_block_peak = context.native_block_peak,
        .native_allocations = context.native_allocations,
        .working_charge_peak = context.working_charge_peak,
    };
}
} // namespace docenhance::io
