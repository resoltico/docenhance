// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "tiff_context.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/tiff.hpp"
#include "jpeg_context.hpp"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <docenhance_tiff.h>
#include <span>
#include <string_view>
#include <tiffio.h>
#include <utility>
namespace docenhance::io {
namespace {
TiffContext& context(thandle_t handle) noexcept {
    return *static_cast<TiffContext*>(handle);
}
tmsize_t read(thandle_t handle, void* const destination, tmsize_t size) {
    auto& state = context(handle);
    if (std::cmp_less(size, 0) || std::cmp_greater(size, state.bytes.size() - state.position)) {
        state.failed = true;
        return 0;
    }
    auto output =
        std::span{static_cast<std::uint8_t*>(destination), static_cast<std::size_t>(size)};
    while (!output.empty()) {
        if (state.cancellation.get().requested(core::Checkpoint::decode)) {
            state.cancelled = true;
            return 0;
        }
        constexpr std::size_t transfer = std::size_t{64} * 1024;
        const auto part = output.first(std::min(output.size(), transfer));
        std::ranges::copy(state.bytes.subspan(state.position, part.size()), part.begin());
        state.position += part.size();
        output = output.subspan(part.size());
    }
    return size;
}
tmsize_t write(thandle_t handle, void* /*source*/, tmsize_t /*size*/) noexcept {
    context(handle).failed = true;
    return 0;
}
toff_t seek(thandle_t handle, toff_t offset, int origin) noexcept {
    auto& state = context(handle);
    std::uint64_t base = 0;
    if (origin == SEEK_CUR) {
        base = state.position;
    } else if (origin == SEEK_END) {
        base = state.bytes.size();
    } else if (origin != SEEK_SET) {
        state.failed = true;
        return static_cast<toff_t>(-1);
    }
    // Native toff_t encodes negative relative displacement modulo 2^64.
    const auto position = base + offset;
    if ((origin == SEEK_SET && offset > state.bytes.size()) || position > state.bytes.size()) {
        state.failed = true;
        return static_cast<toff_t>(-1);
    }
    state.position = static_cast<std::size_t>(position);
    return position;
}
int close(thandle_t /*handle*/) noexcept {
    return 0;
}
toff_t size(thandle_t handle) noexcept {
    return context(handle).bytes.size();
}
#if defined(__clang__) || defined(__GNUC__)
__attribute__((format(printf, 4, 0)))
#endif
int diagnostic(TIFF* /*decoder*/, void* const handle, const char* const module,
               const char* const format, va_list arguments) noexcept {
    auto& state = context(handle);
    // Render only into fixed control storage to classify the locked allocation failures.
    const std::string_view name = module == nullptr ? "" : module;
    constexpr std::size_t diagnostic_bytes = 512;
    std::array<char, diagnostic_bytes> rendered{};
    if (format != nullptr) {
        static_cast<void>(std::vsnprintf(rendered.data(), rendered.size(), format, arguments));
    }
    const std::string_view message{rendered.data()};
    if (name.starts_with("_TIFFmalloc") || name.starts_with("_TIFFcalloc") ||
        name.starts_with("_TIFFrealloc") || message.contains("Out of memory") ||
        message.contains("Memory allocation") || message.contains("Insufficient memory") ||
        message.contains("insufficient memory") || message.starts_with("No space") ||
        message.starts_with("Failed to allocate")) {
        state.exhausted = true;
    } else if (!state.cancelled && !state.jpeg.cancelled) {
        state.failed = true;
        state.jpeg.invalid = true;
    }
    return 1;
}
#if defined(__clang__) || defined(__GNUC__)
__attribute__((format(printf, 4, 0)))
#endif
int warning(TIFF* const decoder, void* const owner, const char* const module,
            const char* const format, va_list arguments) noexcept {
    // The pinned notice concerns interchange conformance, not damaged pixels. Bounded Huffman
    // progressive decoding uses the same charged allocator, scan cap and fixed output policy.
    constexpr std::string_view progressive_notice =
        "The JPEG strip/tile is encoded with progressive mode, ";
    if (module != nullptr && format != nullptr && std::string_view{module} == "JPEGPreDecode" &&
        std::string_view{format}.starts_with(progressive_notice)) {
        return 1;
    }
    return diagnostic(decoder, owner, module, format, arguments);
}
int prepare(void* const owner, void* const decoder) noexcept {
    auto& state = *static_cast<TiffContext*>(owner);
    auto* const native = static_cast<j_decompress_ptr>(decoder);
    const bool sequential_dct = native->Ss == 0 && native->Se == DCTSIZE2 - 1;
    if (std::cmp_not_equal(native->data_precision, image::byte_bits) ||
        native->arith_code != FALSE || (native->progressive_mode == FALSE && !sequential_dct)) {
        state.failed = true;
        state.jpeg.invalid = true;
        return 0;
    }
    if (state.cancellation.get().requested(core::Checkpoint::decode)) {
        state.cancelled = true;
        return 0;
    }
    native->scale_num = 1;
    native->scale_denom = 1;
    native->dct_method = JDCT_ISLOW;
    native->do_fancy_upsampling = TRUE;
    native->do_block_smoothing = FALSE;
    return 1;
}
void install(void* const owner, void* const decoder) noexcept {
    auto& state = *static_cast<TiffContext*>(owner);
    auto* const native = static_cast<j_common_ptr>(decoder);
    native->client_data = &state.jpeg;
    install_jpeg_memory(state.jpeg, native);
    state.jpeg.scan_limit = image::jpeg_scan_max;
    install_jpeg_progress(state.jpeg, static_cast<j_decompress_ptr>(decoder));
}
} // namespace
bool open_tiff(TiffContext& state) {
    auto* const options = TIFFOpenOptionsAlloc();
    if (options == nullptr) {
        state.exhausted = true;
        return false;
    }
    TIFFOpenOptionsSetMaxSingleMemAlloc(options, tiff_native_payload_max);
    TIFFOpenOptionsSetMaxCumulatedMemAlloc(options, tiff_native_payload_max);
    TIFFOpenOptionsSetErrorHandlerExtR(options, diagnostic, &state);
    TIFFOpenOptionsSetWarningHandlerExtR(options, warning, &state);
    TIFFOpenOptionsSetWarnAboutUnknownTags(options, 0);
    state.decoder = TIFFClientOpenExt("identified-source", "rBmc", &state, read, write, seek, close,
                                      size, nullptr, nullptr, options);
    TIFFOpenOptionsFree(options);
    return state.decoder != nullptr && state.good();
}
TiffContext::~TiffContext() {
    if (decoder != nullptr) {
        TIFFClose(decoder);
    }
}
bool TiffContext::good() const noexcept {
    return !failed && !exhausted && !cancelled && !jpeg.exhausted && !jpeg.cancelled;
}
core::Error TiffContext::error() const {
    if (exhausted || jpeg.exhausted) {
        return {
            .code = core::ErrorCode::resource,
            .message = "TIFF native allocation limit exceeded",
        };
    }
    if (failed) {
        return {.code = core::ErrorCode::input, .message = "TIFF is corrupt or unsupported"};
    }
    if (cancelled || jpeg.cancelled) {
        return core::cancelled().error();
    }
    return {.code = core::ErrorCode::input, .message = "TIFF decoding failed"};
}
// The installer object is borrowed by libtiff until this synchronous decode returns.
void bind_tiff_jpeg(TiffContext& state, DocEnhanceTiffJpegControl& installer) {
    installer = {.install = install, .prepare = prepare, .context = &state};
    TIFFSetClientInfo(state.decoder, &installer, DOCENHANCE_TIFF_JPEG_CONTROL);
}
} // namespace docenhance::io
