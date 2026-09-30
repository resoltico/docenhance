// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "jpeg_context.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <algorithm>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <jerror.h>
#include <span>
#include <utility>
namespace docenhance::io {
JpegContext& jpeg_context(j_common_ptr decoder) noexcept {
    return *static_cast<JpegContext*>(decoder->client_data);
}
[[noreturn]] void jpeg_failure(j_common_ptr decoder) noexcept {
    if (decoder->err->msg_code == JERR_OUT_OF_MEMORY) {
        jpeg_context(decoder).exhausted = true;
    }
    // The jump lands in a native-call wrapper; all C++ owners live in its caller.
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    std::longjmp(std::begin(jpeg_context(decoder).jump), 1);
}
void jpeg_checkpoint(j_common_ptr decoder) noexcept {
    auto& context = jpeg_context(decoder);
    if (context.cancellation.get().requested(core::Checkpoint::decode)) {
        context.cancelled = true;
        jpeg_failure(decoder);
    }
}
namespace {
j_common_ptr common(j_decompress_ptr decoder) noexcept {
    // libjpeg defines a shared initial layout for its native codec structs.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<j_common_ptr>(decoder);
}
void warning(j_common_ptr decoder, int level) noexcept {
    if (level < 0) {
        jpeg_failure(decoder);
    }
}
void silent(j_common_ptr /*decoder*/) noexcept {
    // Error delivery belongs to the typed result, never native stderr.
}
void monitor(j_common_ptr decoder) noexcept {
    jpeg_checkpoint(decoder);
    auto& context = jpeg_context(decoder);
    if (std::cmp_greater(context.decoder.input_scan_number, context.scan_limit)) {
        context.exhausted = true;
        jpeg_failure(decoder);
    }
}
void begin_source(j_decompress_ptr /*decoder*/) noexcept {
    // The immutable source is initialized by the enclosing native-call wrapper.
}
boolean fill_source(j_decompress_ptr decoder) noexcept {
    auto& context = *static_cast<JpegContext*>(decoder->client_data);
    // libjpeg's memory source fabricates an EOI at EOF. This source never repairs input.
    if (context.remaining.empty()) {
        decoder->err->error_exit(common(decoder));
    }
    jpeg_checkpoint(common(decoder));
    constexpr std::size_t transfer = std::size_t{64} * 1024;
    const auto part = context.remaining.first(std::min(transfer, context.remaining.size()));
    context.source.next_input_byte = part.data();
    context.source.bytes_in_buffer = part.size();
    context.remaining = context.remaining.subspan(part.size());
    return TRUE;
}
void skip_source(j_decompress_ptr decoder,
                 decltype(jpeg_progress_mgr::pass_counter) count) noexcept {
    if (count <= 0) {
        return;
    }
    auto& context = *static_cast<JpegContext*>(decoder->client_data);
    auto remaining = static_cast<std::size_t>(count);
    while (remaining > context.source.bytes_in_buffer) {
        remaining -= context.source.bytes_in_buffer;
        static_cast<void>(fill_source(decoder));
    }
    const std::span bytes{context.source.next_input_byte, context.source.bytes_in_buffer};
    context.source.next_input_byte = bytes.subspan(remaining).data();
    context.source.bytes_in_buffer -= remaining;
}
void finish_source(j_decompress_ptr /*decoder*/) noexcept {
    // Framing, final EOI and trailing-byte refusal were checked before native decoding.
}
} // namespace
void install_jpeg_source(JpegContext& context, std::span<const std::uint8_t> bytes) noexcept {
    context.remaining = bytes;
    context.source = {
        .next_input_byte = nullptr,
        .bytes_in_buffer = 0,
        .init_source = begin_source,
        .fill_input_buffer = fill_source,
        .skip_input_data = skip_source,
        .resync_to_restart = jpeg_resync_to_restart,
        .term_source = finish_source,
    };
    context.decoder.src = &context.source;
    context.progress.progress_monitor = monitor;
    context.decoder.progress = &context.progress;
}
bool jpeg_header(JpegContext& context, std::span<const std::uint8_t> bytes) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(std::begin(context.jump)) != 0) {
        return false;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    context.decoder.client_data = &context;
    context.decoder.err = jpeg_std_error(&context.errors);
    context.errors.error_exit = jpeg_failure;
    context.errors.emit_message = warning;
    context.errors.output_message = silent;
    jpeg_create_decompress(&context.decoder);
    install_jpeg_memory(context);
    install_jpeg_source(context, bytes);
    return jpeg_read_header(&context.decoder, TRUE) == JPEG_HEADER_OK;
}
bool jpeg_pixels(JpegContext& context, image::PlaneView<std::uint8_t> pixels) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(std::begin(context.jump)) != 0) {
        return false;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    context.decoder.out_color_space =
        context.decoder.num_components == 1 ? JCS_GRAYSCALE : JCS_EXT_RGB;
    context.decoder.scale_num = 1;
    context.decoder.scale_denom = 1;
    context.decoder.dct_method = JDCT_ISLOW;
    context.decoder.do_fancy_upsampling = TRUE;
    context.decoder.do_block_smoothing = FALSE;
    if (jpeg_start_decompress(&context.decoder) == FALSE ||
        context.decoder.output_height != pixels.height() ||
        std::uint64_t{context.decoder.output_width} *
                static_cast<unsigned>(context.decoder.output_components) !=
            pixels.width()) {
        return false;
    }
    while (context.decoder.output_scanline < context.decoder.output_height) {
        const auto row = pixels.row(context.decoder.output_scanline);
        JSAMPROW pointer = row.data();
        if (jpeg_read_scanlines(&context.decoder, &pointer, 1) != 1) {
            return false;
        }
    }
    if (context.decoder.progressive_mode != FALSE) {
        for (int component = 0; component < context.decoder.num_components; ++component) {
            const auto bands = std::span{context.decoder.coef_bits,
                                         static_cast<std::size_t>(context.decoder.num_components)};
            if (bands.subspan(static_cast<std::size_t>(component), 1).front()[0] < 0) {
                return false;
            }
        }
    }
    return jpeg_finish_decompress(&context.decoder) != FALSE;
}
JpegContext::~JpegContext() {
    if (decoder.mem != nullptr) {
        jpeg_destroy_decompress(&decoder);
    }
}
core::Error JpegContext::error() const {
    if (exhausted) {
        return {
            .code = core::ErrorCode::resource,
            .message = "JPEG exceeds its memory or scan limit",
        };
    }
    if (cancelled) {
        return core::cancelled().error();
    }
    return {.code = core::ErrorCode::input, .message = "JPEG is corrupt, truncated or unsupported"};
}
} // namespace docenhance::io
