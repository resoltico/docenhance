// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "docenhance/io/png.hpp"
#include "png_context.hpp"
#include "png_metadata.hpp"

#include <algorithm>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <png.h>
#include <pngconf.h>
#include <span>
#include <utility>

namespace docenhance::io {
namespace {
// The immutable stream was fully CRC/framing checked by scan_png. Present only pixel chunks:
// libpng must not reinterpret, warn away or reject the separately owned color policy.
struct PixelStream {
    std::span<const std::uint8_t> remaining;
    std::span<const std::uint8_t> pending;
    explicit PixelStream(std::span<const std::uint8_t> bytes)
        : remaining(bytes.subspan(png_signature_bytes)), pending(bytes.first(png_signature_bytes)) {
    }
    bool read(std::span<std::uint8_t> output) noexcept {
        while (!output.empty()) {
            while (pending.empty()) {
                if (remaining.empty()) {
                    return false;
                }
                const auto count = png_integer(remaining) + png_chunk_overhead;
                const auto type = png_integer(remaining.subspan(png_integer_bytes));
                if (pixel_chunk(type)) {
                    pending = remaining.first(count);
                }
                remaining = remaining.subspan(count);
            }
            const auto count = std::min(output.size(), pending.size());
            std::ranges::copy(pending.first(count), output.begin());
            output = output.subspan(count);
            pending = pending.subspan(count);
        }
        return true;
    }
};
void read_pixels_bytes(png_structp png, png_bytep bytes, png_size_t count) noexcept {
    auto& input = *static_cast<PixelStream*>(png_get_io_ptr(png));
    constexpr std::size_t transfer = std::size_t{64} * 1024;
    auto output = std::span{bytes, count};
    while (!output.empty()) {
        if (observe_cancellation(png, core::Checkpoint::decode)) {
            png_error(png, "Cancelled");
        }
        auto const part = output.first(std::min(transfer, output.size()));
        if (!input.read(part)) {
            png_error(png, "Truncated PNG pixel stream");
        }
        output = output.subspan(part.size());
    }
}
bool raster_header(PngContext& context, PixelStream& input) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(png_jmpbuf(context.png)) != 0) {
        return false;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    png_set_read_fn(context.png, &input, read_pixels_bytes);
    png_set_crc_action(context.png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
    png_read_info(context.png, context.info);
    if (png_get_color_type(context.png, context.info) == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(context.png);
    }
    png_set_expand_gray_1_2_4_to_8(context.png);
    if (png_get_valid(context.png, context.info, PNG_INFO_tRNS) != 0) {
        png_set_tRNS_to_alpha(context.png);
    }
    context.passes = png_set_interlace_handling(context.png);
    png_read_update_info(context.png, context.info);
    return true;
}
bool raster_pixels(PngContext const& context, image::PlaneView<std::uint8_t> rows) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(png_jmpbuf(context.png)) != 0) {
        return false;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    for (int pass = 0; pass < context.passes; ++pass) {
        for (std::uint32_t y = 0; y < rows.height(); ++y) {
            if (observe_cancellation(context.png, core::Checkpoint::decode)) {
                return false;
            }
            png_read_row(context.png, rows.row(y).data(), nullptr);
        }
    }
    png_read_end(context.png, context.info);
    return true;
}
image::SampleModel sample_model(const PngContext& context) noexcept {
    switch (png_get_color_type(context.png, context.info)) {
    case PNG_COLOR_TYPE_GRAY:
        return image::SampleModel::gray;
    case PNG_COLOR_TYPE_GRAY_ALPHA:
        return image::SampleModel::gray_alpha;
    case PNG_COLOR_TYPE_RGB:
        return image::SampleModel::rgb;
    default:
        return image::SampleModel::rgba;
    }
}
} // namespace
core::Result<image::Raster> decode_png_raster(std::span<const std::uint8_t> bytes,
                                              core::Budget& budget, image::ProfilePolicy policy,
                                              const core::Cancellation& cancellation,
                                              PngLimits limits) {
    auto scanned = scan_png(bytes, budget, policy, cancellation, limits);
    if (!scanned) {
        return std::unexpected(std::move(scanned.error()));
    }
    PixelStream input{bytes};
    PngContext context{budget, false, cancellation};
    if (context.png == nullptr || context.info == nullptr || !raster_header(context, input)) {
        return std::unexpected(context.error(core::ErrorCode::input));
    }
    auto shape = scanned->shape;
    shape.depth = png_get_bit_depth(context.png, context.info);
    shape.model = sample_model(context);
    auto size = image::raster_row_bytes(shape);
    if (!size) {
        return std::unexpected(size.error());
    }
    if (png_get_rowbytes(context.png, context.info) != *size) {
        return core::failure(core::ErrorCode::input, "PNG row size differs from its description");
    }
    auto pixels = image::Plane<std::uint8_t>::allocate(budget, *size, shape.height);
    if (!pixels) {
        return std::unexpected(pixels.error());
    }
    for (std::uint32_t y = 0; y < shape.height; ++y) {
        if (cancellation.requested(core::Checkpoint::decode)) {
            return core::cancelled();
        }
        std::ranges::fill(pixels->view().row(y), 0);
    }
    if (!raster_pixels(context, pixels->view())) {
        return std::unexpected(context.error(core::ErrorCode::input));
    }
    return image::Raster{
        .shape = shape,
        .pixels = std::move(*pixels),
        .metadata = std::move(scanned->metadata),
    };
}
} // namespace docenhance::io
