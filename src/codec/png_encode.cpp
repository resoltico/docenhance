// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "png_context.hpp"
#include "png_rows.hpp"

#include <algorithm>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <iterator>
#include <png.h>
#include <pngconf.h>
#include <span>

namespace docenhance::codec {
namespace {
void write_bytes(png_structp png, png_bytep bytes, png_size_t count) noexcept {
    const auto& context = *static_cast<PngContext*>(png_get_io_ptr(png));
    constexpr std::size_t transfer_bytes = std::size_t{64} * 1024;
    auto input = std::span{bytes, count};
    while (!input.empty()) {
        if (observe_cancellation(png, core::Checkpoint::encode)) {
            png_error(png, "Cancelled");
        }
        const auto chunk = input.first(std::min(transfer_bytes, input.size()));
        if (!context.output.write(context.output.state, chunk)) {
            png_error(png, "Cannot write the PNG output");
        }
        input = input.subspan(chunk.size());
    }
}
void flush_bytes(png_structp png) noexcept {
    const auto& context = *static_cast<PngContext*>(png_get_io_ptr(png));
    if (!context.output.flush(context.output.state)) {
        png_error(png, "Cannot flush the PNG output");
    }
}
[[nodiscard]] bool write_pixels(PngContext& context, image::PlaneView<const std::uint8_t> view) {
    // libpng requires a C jump frame; all C++ owners are in the caller.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(std::begin(context.memory.jump)) != 0) {
        return false;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    install_png_writer(context);
    png_set_IHDR(context.png, context.info, view.width(), view.height(), byte_depth,
                 PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(context.png, context.info);
    for (std::uint32_t row = 0; row < view.height(); ++row) {
        if (observe_cancellation(context.png, core::Checkpoint::encode)) {
            return false;
        }
        png_write_row(context.png, view.row(row).data());
    }
    png_write_end(context.png, context.info);
    return true;
}
} // namespace
void install_png_writer(PngContext& context) {
    png_set_write_fn(context.png, &context, write_bytes, flush_bytes);
}
core::Result<void> encode_png(PngOutput output, image::PlaneView<const std::uint8_t> view,
                              core::Budget& budget, const core::Cancellation& cancellation) {
    if (view.empty()) {
        return core::failure(core::ErrorCode::argument, "Cannot encode an empty image");
    }
    if (output.open == nullptr || output.write == nullptr || output.flush == nullptr) {
        return core::failure(core::ErrorCode::argument, "Incomplete PNG byte stream");
    }
    if (cancellation.requested(core::Checkpoint::encode)) {
        return core::cancelled();
    }
    PngContext context{budget, true, cancellation};
    context.output = output;
    if (!context.ready() || !output.open(output.state) || !write_pixels(context, view)) {
        return std::unexpected(context.error(core::ErrorCode::output));
    }
    return {};
}
} // namespace docenhance::codec
