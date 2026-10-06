// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "tiff_context.hpp"
#include "tiff_ifd.hpp"
#include "tiff_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <tiff.h>
#include <tiffio.h>
#include <utility>
namespace docenhance::io {
namespace {
constexpr unsigned scatter_checkpoint_samples = 1024;
struct Unit {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t height{};
    unsigned plane{};
};
std::uint16_t sample(std::span<const std::byte> row, unsigned depth, std::size_t index) noexcept {
    if (depth == 1) {
        return ((std::to_integer<unsigned>(row.subspan(index / image::byte_bits, 1).front()) >>
                 ((image::byte_bits - 1) - (index % image::byte_bits))) &
                1U) != 0
                   ? image::byte_max
                   : 0;
    }
    if (depth == image::byte_bits) {
        return std::to_integer<std::uint8_t>(row.subspan(index, 1).front());
    }
    std::uint16_t value{};
    // Encoded-strip/tile APIs return host-endian 16-bit samples, including predictor reversal.
    std::memcpy(&value, row.subspan(index * sizeof(value), sizeof(value)).data(), sizeof(value));
    return value;
}
struct StoredPixel {
    std::span<const std::byte> row;
    std::size_t x{};
    unsigned plane{};
};
void put_pixel(const TiffContext& context, const image::TiffSource& source,
               image::RasterShape shape, std::span<std::uint8_t> pixel, StoredPixel stored) {
    const auto channels = image::components(shape.model);
    const auto bytes = shape.depth.bytes();
    const unsigned unit_channels = source.planar == 2 ? 1 : source.samples;
    const auto palette = source.photometric == image::tiff_palette
                             ? context.ifd.field(TIFFTAG_COLORMAP)
                             : std::nullopt;
    for (unsigned c = 0; c < unit_channels; ++c) {
        const auto value = sample(stored.row, source.depth, (stored.x * unit_channels) + c);
        if (palette) {
            for (unsigned channel = 0; channel < channels; ++channel) {
                const auto entry = ((std::size_t{channel} * (image::byte_max + 1)) + value) * 2;
                image::write_sample(pixel.subspan(std::size_t{channel} * bytes, bytes), shape.depth,
                                    static_cast<std::uint16_t>(
                                        context.ifd.number(palette->bytes.subspan(entry, 2))));
            }
        } else {
            const auto channel = source.planar == 2 ? stored.plane : c;
            image::write_sample(pixel.subspan(std::size_t{channel} * bytes, bytes), shape.depth,
                                value);
        }
    }
}
core::Result<void> scatter(const TiffContext& context, const image::TiffSource& source,
                           image::Raster& raster, std::span<const std::byte> decoded, Unit unit) {
    const auto& layout = context.layout;
    const auto columns = std::min(layout.width, source.width - unit.x);
    const auto rows = std::min(unit.height, source.height - unit.y);
    const auto stride = image::components(raster.shape.model) * raster.shape.depth.bytes();
    for (std::uint32_t y = 0; y < rows; ++y) {
        const auto input = decoded.subspan(std::size_t{y} * layout.row_bytes, layout.row_bytes);
        const auto output = raster.pixels.view().row(unit.y + y);
        for (std::uint32_t x = 0; x < columns; ++x) {
            if (x % scatter_checkpoint_samples == 0 &&
                context.cancellation.get().requested(core::Checkpoint::decode)) {
                return std::unexpected(core::cancelled().error());
            }
            put_pixel(context, source, raster.shape,
                      output.subspan(std::size_t{unit.x + x} * stride, stride),
                      {.row = input, .x = x, .plane = unit.plane});
        }
    }
    return {};
}
} // namespace
core::Result<void> tiff_pixels(TiffContext const& context, const image::TiffSource& source,
                               image::Raster& raster, core::Budget& budget) {
    const auto& layout = context.layout;
    if (context.cancellation.get().requested(core::Checkpoint::allocation)) {
        return std::unexpected(core::cancelled().error());
    }
    auto storage = budget.allocate(layout.unit_bytes);
    if (!storage) {
        return std::unexpected(storage.error());
    }
    const auto units_per_plane = layout.across * layout.down;
    for (unsigned index = 0; index < layout.units; ++index) {
        if (context.cancellation.get().requested(core::Checkpoint::decode)) {
            return std::unexpected(core::cancelled().error());
        }
        const auto local = index % units_per_plane;
        const Unit unit{
            .x = (local % layout.across) * layout.width,
            .y = (local / layout.across) * layout.height,
            .height = source.tiled
                          ? layout.height
                          : std::min(layout.height,
                                     source.height - (local / layout.across) * layout.height),
            .plane = index / units_per_plane,
        };
        const auto expected = std::size_t{unit.height} * layout.row_bytes;
        const auto decoded =
            source.tiled ? TIFFReadEncodedTile(context.decoder, index, storage->bytes().data(),
                                               static_cast<tmsize_t>(expected))
                         : TIFFReadEncodedStrip(context.decoder, index, storage->bytes().data(),
                                                static_cast<tmsize_t>(expected));
        if (std::cmp_less(decoded, 0) || !context.good()) {
            return std::unexpected(context.error());
        }
        if (std::cmp_not_equal(decoded, expected)) {
            return core::failure(core::ErrorCode::input, "TIFF decoded unit is incomplete");
        }
        auto copied = scatter(context, source, raster, storage->bytes().first(expected), unit);
        if (!copied) {
            return copied;
        }
    }
    return {};
}
} // namespace docenhance::io
