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

#include <csetjmp>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <png.h>
#include <pngconf.h>
#include <span>

namespace docenhance::io {
namespace {
bool begin_rows(PngContext& context, image::OutputDescriptor description) {
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
    install_png_writer(context);
    const auto shape = description.shape;
    const int type =
        shape.model == image::SampleModel::gray ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_RGB;
    png_set_IHDR(context.png, context.info, shape.width, shape.height,
                 static_cast<int>(shape.depth), type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    constexpr int compression_level = 6;
    constexpr int default_strategy = 0;
    png_set_compression_level(context.png, compression_level);
    png_set_compression_strategy(context.png, default_strategy);
    png_set_filter(context.png, PNG_FILTER_TYPE_BASE, PNG_FILTER_SUB);
    png_set_iCCP(context.png, context.info, output_profile_name, PNG_COMPRESSION_TYPE_DEFAULT,
                 description.profile.data(), static_cast<png_uint_32>(description.profile.size()));
    if (description.resolution) {
        png_set_pHYs(context.png, context.info, description.resolution->x,
                     description.resolution->y, PNG_RESOLUTION_METER);
    }
    png_write_info(context.png, context.info);
    return true;
}
bool write_row(PngContext const& context, std::span<const std::uint8_t> row) {
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
    png_write_row(context.png, row.data());
    return true;
}
bool finish_rows(PngContext const& context) {
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
    png_write_end(context.png, context.info);
    return true;
}
} // namespace
core::Result<void> encode_png_rows(const std::filesystem::path& path, image::RowSource& source,
                                   core::Budget& budget, const core::Cancellation& cancellation) {
    const auto description = source.descriptor();
    const auto bytes = image::raster_row_bytes(description.shape);
    if (!bytes || image::has_alpha(description.shape.model) || description.profile.empty() ||
        description.profile.size() > image::profile_limit) {
        return core::failure(core::ErrorCode::argument, "Invalid opaque PNG output description");
    }
    auto row = image::Plane<std::uint8_t>::allocate(budget, *bytes, 1);
    if (!row) {
        return std::unexpected(row.error());
    }
    if (cancellation.requested(core::Checkpoint::encode)) {
        return core::cancelled();
    }
    PngContext context{budget, true, cancellation};
    if (!context.open(path) || !begin_rows(context, description)) {
        return std::unexpected(context.error(core::ErrorCode::output));
    }
    for (std::uint32_t y = 0; y < description.shape.height; ++y) {
        if (cancellation.requested(core::Checkpoint::encode)) {
            return core::cancelled();
        }
        // Color work and Result ownership are outside every native setjmp frame.
        auto generated = source.row(y, row->view().row(0), image::RowUse::output);
        if (!generated) {
            return generated;
        }
        if (!write_row(context, row->view().row(0))) {
            return std::unexpected(context.error(core::ErrorCode::output));
        }
    }
    if (!finish_rows(context)) {
        return std::unexpected(context.error(core::ErrorCode::output));
    }
    if (!context.close_output()) {
        return core::failure(core::ErrorCode::output, "Cannot close the continuous-tone PNG");
    }
    return {};
}
} // namespace docenhance::io
