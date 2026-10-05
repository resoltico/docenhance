// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/raster.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"

#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace docenhance::image {
unsigned components(SampleModel model) noexcept {
    switch (model) {
    case SampleModel::gray:
        return 1;
    case SampleModel::gray_alpha:
        return 2;
    case SampleModel::rgb:
        return rgb_channels;
    case SampleModel::rgba:
        return rgba_channels;
    }
    return 0;
}
bool is_color(SampleModel model) noexcept {
    return model == SampleModel::rgb || model == SampleModel::rgba;
}
bool has_alpha(SampleModel model) noexcept {
    return model == SampleModel::gray_alpha || model == SampleModel::rgba;
}
core::Result<std::uint32_t> raster_row_bytes(RasterShape shape) {
    const auto channels = components(shape.model);
    if (shape.width == 0 || shape.height == 0 || channels == 0) {
        return core::failure(core::ErrorCode::argument, "Invalid integer raster description");
    }
    const auto bytes = std::uint64_t{shape.width} * channels * shape.depth.bytes();
    if (bytes > std::numeric_limits<std::uint32_t>::max()) {
        return core::failure(core::ErrorCode::resource, "The raster row exceeds its storage range");
    }
    return static_cast<std::uint32_t>(bytes);
}
RasterShape oriented_shape(RasterShape shape, Orientation orientation) noexcept {
    if (orientation.transposed()) {
        std::swap(shape.width, shape.height);
    }
    return shape;
}
Coordinate source_coordinate(RasterShape shape, Orientation orientation, Coordinate p) noexcept {
    if (orientation.transposed()) {
        std::swap(p.x, p.y);
    }
    if (orientation.mirrored_x()) {
        p.x = shape.width - 1 - p.x;
    }
    if (orientation.mirrored_y()) {
        p.y = shape.height - 1 - p.y;
    }
    return p;
}
std::uint16_t read_sample(std::span<const std::uint8_t> bytes, SampleDepth depth) noexcept {
    return depth == SampleDepth::byte()
               ? bytes.front()
               : static_cast<std::uint16_t>((std::uint32_t{bytes.front()} << byte_bits) |
                                            bytes.subspan(1).front());
}
void write_sample(std::span<std::uint8_t> bytes, SampleDepth depth, std::uint16_t sample) noexcept {
    if (depth == SampleDepth::word()) {
        bytes.front() = static_cast<std::uint8_t>(sample >> byte_bits);
        bytes = bytes.subspan(1);
    }
    bytes.front() = static_cast<std::uint8_t>(sample & byte_max);
}
} // namespace docenhance::image
