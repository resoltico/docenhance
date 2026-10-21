// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/png_output.hpp"
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "encoded_bytes.hpp"
#include "png_context.hpp"
#include "png_memory_output.hpp"
#include "png_rows.hpp"

#include <algorithm>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <png.h>
#include <pngconf.h>
#include <span>
#include <utility>

namespace docenhance::codec {
namespace {
bool valid_image(PngImage image) noexcept {
    const auto shape = image.shape;
    if (!image::validate(shape) || image.pixels.empty() || image.pixels.height() != shape.height ||
        image.profile.size() > image::profile_limit ||
        (image.resolution && (image.resolution->x == 0 || image.resolution->y == 0))) {
        return false;
    }
    const auto row_bytes = std::uint64_t{shape.width} * image::components(shape.model) *
                           (shape.depth.bits() == 1 ? 1 : shape.depth.bits() / image::byte_bits);
    if (row_bytes != image.pixels.width()) {
        return false;
    }
    if (shape.depth.bits() == 1) {
        return image.profile.empty();
    }
    return !image.profile.empty() || (shape.model == image::SampleModel::gray &&
                                      shape.depth.bits() == image::byte_bits && !image.resolution);
}
bool begin(PngContext& context, PngImage image) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // Every owner is in write_png; this frame and native callees retain only trivial borrows.
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(std::begin(context.memory.jump)) != 0) {
        return false;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    install_png_writer(context);
    png_set_IHDR(context.png, context.info, image.shape.width, image.shape.height,
                 static_cast<int>(image.shape.depth.bits()),
                 image.shape.model == image::SampleModel::gray ? PNG_COLOR_TYPE_GRAY
                                                               : PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    constexpr int compression_level = 6;
    constexpr int default_strategy = 0;
    png_set_compression_level(context.png, compression_level);
    png_set_compression_strategy(context.png, default_strategy);
    png_set_filter(context.png, PNG_FILTER_TYPE_BASE, PNG_FILTER_SUB);
    if (!image.profile.empty()) {
        png_set_iCCP(context.png, context.info, output_profile_name, PNG_COMPRESSION_TYPE_DEFAULT,
                     encoded_bytes(image.profile).data(),
                     static_cast<png_uint_32>(image.profile.size()));
    }
    if (image.resolution) {
        png_set_pHYs(context.png, context.info, image.resolution->x, image.resolution->y,
                     PNG_RESOLUTION_METER);
    }
    png_write_info(context.png, context.info);
    return true;
}
bool write_row(PngContext& context, std::span<const std::uint8_t> row) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // The borrowed row and native context owners are outside this jump frame.
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(std::begin(context.memory.jump)) != 0) {
        return false;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    png_write_row(context.png, row.data());
    return true;
}
bool finish(PngContext& context) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // Native finish has no local C++ owner to bypass on error.
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(std::begin(context.memory.jump)) != 0) {
        return false;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    png_write_end(context.png, context.info);
    return true;
}
core::Result<void> logical_row(std::span<const std::uint8_t> input, std::span<std::uint8_t> packed,
                               const core::Cancellation& cancellation) {
    std::ranges::fill(packed, 0);
    for (std::size_t x = 0; x < input.size(); ++x) {
        constexpr std::size_t interval = 1024;
        if (x % interval == 0 && cancellation.requested(core::Checkpoint::encode)) {
            return core::cancelled();
        }
        const auto code = input.subspan(x, 1).front();
        if (code > 1) {
            return core::failure(core::ErrorCode::argument, "Invalid logical PNG sample");
        }
        if (!packed.empty()) {
            packed.subspan(x / image::byte_bits, 1).front() |=
                static_cast<std::uint8_t>(code << (image::byte_bits - 1 - (x % image::byte_bits)));
        }
    }
    return {};
}
} // namespace
core::Result<void> write_png(PngOutput output, PngImage image, core::Budget& budget,
                             const core::Cancellation& cancellation) {
    if (!valid_image(image) || output.open == nullptr || output.write == nullptr ||
        output.flush == nullptr) {
        return core::failure(core::ErrorCode::argument, "Invalid materialized PNG output");
    }
    if (cancellation.requested(core::Checkpoint::encode)) {
        return core::cancelled();
    }
    const bool binary = image.shape.depth.bits() == 1;
    const bool logical = binary || image.profile.empty();
    auto row = budget.allocate(
        binary ? (std::size_t{image.shape.width} + image::byte_bits - 1) / image::byte_bits : 0);
    if (!row) {
        return std::unexpected(std::move(row.error()));
    }
    PngContext context{budget, true, cancellation};
    context.output = output;
    if (!context.ready() || !output.open(output.state) || !begin(context, image)) {
        return std::unexpected(context.error(core::ErrorCode::output));
    }
    const auto packed =
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        std::span{reinterpret_cast<std::uint8_t*>(row->bytes().data()), row->size()};
    for (std::uint32_t y = 0; y < image.shape.height; ++y) {
        if (cancellation.requested(core::Checkpoint::encode)) {
            return core::cancelled();
        }
        const auto pixels = image.pixels.row(y);
        if (auto result =
                logical ? logical_row(pixels, packed, cancellation) : core::Result<void>{};
            !result) {
            return result;
        }
        if (!write_row(context, binary ? std::span<const std::uint8_t>{packed} : pixels)) {
            return std::unexpected(context.error(core::ErrorCode::output));
        }
    }
    if (!finish(context)) {
        return std::unexpected(context.error(core::ErrorCode::output));
    }
    return {};
}
} // namespace docenhance::codec
