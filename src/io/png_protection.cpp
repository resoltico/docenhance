// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "docenhance/io/protection_png.hpp"
#include "png_metadata.hpp"
#include "png_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
namespace docenhance::io {
namespace {
core::Result<void> admitted_header(std::span<const std::uint8_t> bytes, image::Extent extent) {
    constexpr std::size_t width_offset = 16;
    constexpr std::size_t height_offset = 20;
    constexpr std::size_t depth_offset = 24;
    constexpr std::size_t color_offset = 25;
    constexpr unsigned gray_alpha_type = 4;
    if (bytes.size() <= color_offset || extent.width == 0 || extent.height == 0) {
        return core::failure(core::ErrorCode::input, "Protection requires a nonempty PNG mask");
    }
    const auto depth = bytes.subspan(depth_offset, 1).front();
    const auto color = bytes.subspan(color_offset, 1).front();
    if ((depth != 1 && depth != image::byte_bits) || (color != 0 && color != gray_alpha_type) ||
        png_integer(bytes.subspan(width_offset)) != extent.width ||
        png_integer(bytes.subspan(height_offset)) != extent.height) {
        return core::failure(core::ErrorCode::input,
                             "Protection must be 1/8-bit gray with oriented source dimensions");
    }
    return {};
}
core::Result<void> normalize(const image::Raster& raster, image::PlaneView<std::uint8_t> mask,
                             const core::Cancellation& cancellation) {
    const auto channels = image::components(raster.shape.model);
    constexpr std::uint32_t poll = 4096;
    for (std::uint32_t y = 0; y < mask.height(); ++y) {
        const auto in = raster.pixels.view().row(y);
        auto const out = mask.row(y);
        for (std::uint32_t x = 0; x < mask.width(); ++x) {
            if (x % poll == 0 && cancellation.requested(core::Checkpoint::decode)) {
                return core::cancelled();
            }
            const auto pixel = in.subspan(std::size_t{x} * channels, channels);
            if (channels == 2 && pixel.subspan(1, 1).front() != image::byte_max) {
                return core::failure(core::ErrorCode::input,
                                     "Protection mask contains nonopaque alpha");
            }
            out.subspan(x, 1).front() = static_cast<std::uint8_t>(pixel.front() != 0);
        }
    }
    return {};
}
} // namespace
core::Result<image::Plane<std::uint8_t>>
decode_protection_png(std::span<const std::uint8_t> bytes, image::Extent extent,
                      core::Budget& budget, const core::Cancellation& cancellation) {
    if (cancellation.requested(core::Checkpoint::decode)) {
        return core::cancelled();
    }
    const auto header = admitted_header(bytes, extent);
    if (!header) {
        return std::unexpected(header.error());
    }
    auto raster = decode_png_raster(bytes, budget, image::ProfilePolicy::srgb, cancellation);
    if (!raster) {
        return std::unexpected(raster.error());
    }
    if (raster->metadata.orientation != 1 || image::is_color(raster->shape.model)) {
        return core::failure(core::ErrorCode::input,
                             "Protection mask must have normal orientation and gray samples");
    }
    auto mask = image::Plane<std::uint8_t>::allocate(budget, extent.width, extent.height);
    if (!mask) {
        return std::unexpected(mask.error());
    }
    const auto normalized = normalize(*raster, mask->view(), cancellation);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }
    return std::move(*mask);
}
core::Result<image::Plane<std::uint8_t>>
load_protection_png(const std::string& path, image::Extent extent, core::Budget& budget,
                    const core::Cancellation& cancellation) {
    auto bytes = read_png_snapshot(path, budget, cancellation);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    // Observe immutable encoded bytes; the backing Buffer owns the lifetime.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes->bytes().data());
    return decode_protection_png({data, bytes->size()}, extent, budget, cancellation);
}
} // namespace docenhance::io
