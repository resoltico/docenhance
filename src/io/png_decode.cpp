// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "png_context.hpp"

#include <algorithm>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <png.h>
#include <string>
#include <system_error>
#include <utility>

namespace docenhance::io {
namespace {
constexpr std::uint64_t max_pixels = 40'000'000;
constexpr std::uintmax_t mebibyte = std::uintmax_t{1024} * 1024;
constexpr std::uintmax_t max_file_bytes = 128 * mebibyte;
constexpr unsigned max_cached_chunks = 32;
constexpr int nibble_depth = 4;
// The jump target contains only trivial automatic state. All owning C++ objects are in its caller.
[[nodiscard]] bool read_header(const PngContext& context) {
    // libpng requires a C jump frame; all C++ owners are in the caller.
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(png_jmpbuf(context.png)) != 0) {
        return false;
    }
    png_init_io(context.png, context.file.get());
    png_set_crc_action(context.png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
    png_set_chunk_malloc_max(context.png, mebibyte);
    png_set_chunk_cache_max(context.png, max_cached_chunks);
    png_read_info(context.png, context.info);
    return true;
}
[[nodiscard]] bool read_pixels(PngContext& context, image::PlaneView<std::uint8_t> view) {
    // libpng requires a C jump frame; all C++ owners are in the caller.
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(png_jmpbuf(context.png)) != 0) {
        return false;
    }
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

core::Result<image::Plane<std::uint8_t>> load_grayscale_png(const std::string& input,
                                                            core::Budget& budget) {
    const auto path = utf8_path(input);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return core::failure(core::ErrorCode::input,
                             "The PNG input must be a readable regular file");
    }
    PngContext context{budget, false};
    if (!context.open(path)) {
        return std::unexpected(context.error(core::ErrorCode::input));
    }
    if (std::fseek(context.file.get(), 0, SEEK_END) != 0) {
        return core::failure(core::ErrorCode::input, "Cannot inspect the PNG input");
    }
    const auto size = static_cast<std::int64_t>(std::ftell(context.file.get()));
    if (size < 0 || std::fseek(context.file.get(), 0, SEEK_SET) != 0) {
        return core::failure(core::ErrorCode::input, "Cannot inspect the PNG input");
    }
    if (std::cmp_greater(size, max_file_bytes)) {
        return core::failure(core::ErrorCode::resource,
                             "The PNG input exceeds the 128 MiB file limit");
    }
    if (!read_header(context)) {
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
    if (pixels == 0 || pixels > max_pixels) {
        return core::failure(core::ErrorCode::resource,
                             "PNG dimensions exceed the 40 megapixel processing limit");
    }
    auto plane = image::Plane<std::uint8_t>::allocate(budget, width, height);
    if (!plane) {
        return std::unexpected(std::move(plane.error()));
    }
    std::ranges::fill(plane->view().storage(), 0);
    if (!read_pixels(context, plane->view())) {
        return std::unexpected(context.error(core::ErrorCode::input));
    }
    return plane;
}
} // namespace docenhance::io
