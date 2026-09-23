// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "png_context.hpp"
#include "png_reader.hpp"

#include <algorithm>
#include <csetjmp>
#include <cstdint>
#include <expected>
#include <png.h>
#include <pngconf.h>
#include <span>
#include <utility>

namespace docenhance::io {
namespace {
constexpr std::uintmax_t mebibyte = std::uintmax_t{1024} * 1024;
constexpr unsigned max_cached_chunks = 32;
constexpr int nibble_depth = 4;
// The jump target contains only trivial automatic state. All owning C++ objects are in its caller.
void read_bytes(png_structp png, png_bytep bytes, png_size_t count) noexcept {
    auto& input = *static_cast<PngInput*>(png_get_io_ptr(png));
    if (count > input.remaining || !input.read(input.state, {bytes, count})) {
        png_error(png, "The PNG input is truncated or exceeds its encoded-byte bound");
    }
    input.remaining -= count;
}
[[nodiscard]] bool read_header(const PngContext& context, PngInput& input) {
    // libpng requires a C jump frame; all C++ owners are in the caller.
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
    png_set_read_fn(context.png, &input, read_bytes);
    png_set_crc_action(context.png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
    png_set_chunk_malloc_max(context.png, mebibyte);
    png_set_chunk_cache_max(context.png, max_cached_chunks);
    png_read_info(context.png, context.info);
    return true;
}
[[nodiscard]] bool read_pixels(PngContext& context, image::PlaneView<std::uint8_t> view) {
    // libpng requires a C jump frame; all C++ owners are in the caller.
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
    // Preserve stored samples: do not apply a gamma transfer or color conversion.
    png_set_expand_gray_1_2_4_to_8(context.png);
    context.passes = png_set_interlace_handling(context.png);
    png_read_update_info(context.png, context.info);
    if (png_get_rowbytes(context.png, context.info) != view.width()) {
        return false;
    }
    for (int pass = 0; pass < context.passes; ++pass) {
        for (std::uint32_t row = 0; row < view.height(); ++row) {
            png_read_row(context.png, view.row(row).data(), nullptr);
        }
    }
    png_read_end(context.png, context.info);
    return true;
}
} // namespace

core::Result<image::Plane<std::uint8_t>> decode_png(PngContext& context, PngInput& input,
                                                    PngLimits limits) {
    const PngLimits ceiling;
    if (limits.encoded_bytes == 0 || limits.pixels == 0 ||
        limits.encoded_bytes > ceiling.encoded_bytes || limits.pixels > ceiling.pixels) {
        return core::failure(core::ErrorCode::argument,
                             "PNG limits must be positive and within production limits");
    }
    if (input.remaining > limits.encoded_bytes) {
        return core::failure(core::ErrorCode::resource,
                             "The PNG input exceeds its encoded-byte limit");
    }
    if (context.png == nullptr || context.info == nullptr) {
        return std::unexpected(context.error(core::ErrorCode::resource));
    }
    if (!read_header(context, input)) {
        return std::unexpected(context.error(core::ErrorCode::input));
    }
    const auto depth = png_get_bit_depth(context.png, context.info);
    if (png_get_color_type(context.png, context.info) != PNG_COLOR_TYPE_GRAY ||
        (depth != 1 && depth != 2 && depth != nibble_depth && depth != byte_depth) ||
        png_get_valid(context.png, context.info, PNG_INFO_tRNS) != 0) {
        return core::failure(
            core::ErrorCode::input,
            "Only 1-, 2-, 4- and 8-bit grayscale PNG without transparency is supported");
    }
    const auto width = png_get_image_width(context.png, context.info);
    const auto height = png_get_image_height(context.png, context.info);
    const auto pixels = static_cast<std::uint64_t>(width) * height;
    if (pixels == 0 || pixels > limits.pixels) {
        return core::failure(core::ErrorCode::resource,
                             "PNG dimensions exceed the processing pixel limit");
    }
    auto plane = image::Plane<std::uint8_t>::allocate(context.memory.budget.get(), width, height);
    if (!plane) {
        return std::unexpected(std::move(plane.error()));
    }
    std::ranges::fill(plane->view().storage(), 0);
    if (!read_pixels(context, plane->view())) {
        return std::unexpected(context.error(core::ErrorCode::input));
    }
    return plane;
}
namespace {
bool read_memory(void* const state, std::span<std::uint8_t> output) noexcept {
    auto& bytes = *static_cast<std::span<const std::uint8_t>*>(state);
    if (output.size() > bytes.size()) {
        return false;
    }
    std::ranges::copy(bytes.first(output.size()), output.begin());
    bytes = bytes.subspan(output.size());
    return true;
}
} // namespace
core::Result<image::Plane<std::uint8_t>>
decode_grayscale_png(std::span<const std::uint8_t> input, core::Budget& budget, PngLimits limits) {
    PngContext context{budget, false};
    PngInput reader{.state = &input, .read = read_memory, .remaining = input.size()};
    return decode_png(context, reader, limits);
}
} // namespace docenhance::io
