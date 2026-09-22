// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "png_context.hpp"

#include <csetjmp>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <png.h>

namespace docenhance::io {
namespace {
[[nodiscard]] bool write_pixels(const PngContext& context,
                                image::PlaneView<const std::uint8_t> view) {
    // libpng requires a C jump frame; all C++ owners are in the caller.
#ifdef _MSC_VER
#pragma warning(suppress : 4611)
#endif
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(png_jmpbuf(context.png)) != 0) {
        return false;
    }
    png_init_io(context.png, context.file.get());
    png_set_IHDR(context.png, context.info, view.width(), view.height(), byte_depth,
                 PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(context.png, context.info);
    for (std::uint32_t row = 0; row < view.height(); ++row) {
        png_write_row(context.png, view.row(row).data());
    }
    png_write_end(context.png, context.info);
    return true;
}
} // namespace
core::Result<void> encode_png(const std::filesystem::path& output,
                              image::PlaneView<const std::uint8_t> view, core::Budget& budget) {
    if (view.empty()) {
        return core::failure(core::ErrorCode::argument, "Cannot encode an empty image");
    }
    PngContext context{budget, true};
    if (!context.open(output) || !write_pixels(context, view)) {
        return std::unexpected(context.error(core::ErrorCode::output));
    }
    if (!context.close_output()) {
        return core::failure(core::ErrorCode::output, "Cannot finish writing the PNG output");
    }
    return {};
}
} // namespace docenhance::io
