// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "png_context.hpp"
#include "png_rows.hpp"

#include <algorithm>
#include <csetjmp>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <png.h>
#include <pngconf.h>
#include <span>
#include <string_view>
#include <utility>

namespace docenhance::io {
namespace {
bool read_output_header(PngContext const& context) {
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
    png_init_io(context.png, context.file.get());
    png_set_crc_action(context.png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
    png_set_chunk_malloc_max(context.png, image::profile_limit);
    png_read_info(context.png, context.info);
    png_read_update_info(context.png, context.info);
    return true;
}
bool matching_header(const PngContext& context, image::OutputDescriptor description) {
    const auto shape = description.shape;
    const auto type =
        shape.model == image::SampleModel::gray ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_RGB;
    if (png_get_image_width(context.png, context.info) != shape.width ||
        png_get_image_height(context.png, context.info) != shape.height ||
        png_get_bit_depth(context.png, context.info) != shape.depth ||
        std::cmp_not_equal(png_get_color_type(context.png, context.info), type) ||
        png_get_interlace_type(context.png, context.info) != PNG_INTERLACE_NONE) {
        return false;
    }
    png_charp name = nullptr;
    png_bytep profile = nullptr;
    png_uint_32 size = 0;
    int compression = 0;
    if (png_get_iCCP(context.png, context.info, &name, &compression, &profile, &size) == 0 ||
        name == nullptr || std::string_view{name} != output_profile_name ||
        !std::ranges::equal(description.profile, std::span{profile, size})) {
        return false;
    }
    png_uint_32 x = 0;
    png_uint_32 y = 0;
    int unit = 0;
    const bool resolution = png_get_pHYs(context.png, context.info, &x, &y, &unit) != 0;
    return resolution == description.resolution.has_value() &&
           (!resolution || (unit == PNG_RESOLUTION_METER && x == description.resolution->x &&
                            y == description.resolution->y));
}
bool read_output_row(PngContext const& context, std::span<std::uint8_t> bytes) {
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
    png_read_row(context.png, bytes.data(), nullptr);
    return true;
}
bool read_output_end(PngContext const& context) {
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
    png_read_end(context.png, context.info);
    return true;
}
} // namespace
core::Result<void> verify_png_rows(const std::filesystem::path& path, image::RowSource& source,
                                   core::Budget& budget, const core::Cancellation& cancellation) {
    const auto description = source.descriptor();
    const auto bytes = image::raster_row_bytes(description.shape);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto rows = image::Plane<std::uint8_t>::allocate(budget, *bytes, 2);
    if (!rows) {
        return std::unexpected(rows.error());
    }
    PngContext context{budget, false, cancellation};
    if (!context.open(path) || !output_inventory(context, cancellation) ||
        !read_output_header(context)) {
        return std::unexpected(context.error(core::ErrorCode::output_verify));
    }
    if (!matching_header(context, description) ||
        png_get_rowbytes(context.png, context.info) != *bytes) {
        return core::failure(core::ErrorCode::output_verify,
                             "Encoded PNG metadata does not match the intended output");
    }
    for (std::uint32_t y = 0; y < description.shape.height; ++y) {
        if (cancellation.requested(core::Checkpoint::verification)) {
            return core::cancelled();
        }
        auto expected = source.row(y, rows->view().row(0), image::RowUse::verification);
        if (!expected) {
            return expected;
        }
        if (!read_output_row(context, rows->view().row(1))) {
            return std::unexpected(context.error(core::ErrorCode::output_verify));
        }
        if (!std::ranges::equal(rows->view().row(0), rows->view().row(1))) {
            return core::failure(core::ErrorCode::output_verify,
                                 "Encoded PNG integer samples failed verification");
        }
    }
    return read_output_end(context)
               ? core::Result<void>{}
               : core::Result<void>{std::unexpected(context.error(core::ErrorCode::output_verify))};
}
} // namespace docenhance::io
