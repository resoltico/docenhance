// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/image/plane.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <string>

namespace docenhance::image {
namespace {
core::Error too_large(const std::string& what) {
    return {
        .code = core::ErrorCode::resource,
        .message = what + " exceeds the largest size this build can address",
    };
}
// The next multiple of the buffer alignment, or nothing if that would overflow.
[[nodiscard]] core::Result<std::size_t> aligned_up(std::size_t bytes) {
    constexpr std::size_t alignment = core::buffer_alignment;
    const std::size_t remainder = bytes % alignment;
    if (remainder == 0) {
        return bytes;
    }
    const std::size_t padding = alignment - remainder;
    if (bytes > std::numeric_limits<std::size_t>::max() - padding) {
        return std::unexpected(too_large("The row size"));
    }
    return bytes + padding;
}
} // namespace

core::Result<PlaneShape> plane_shape(std::uint32_t width, std::uint32_t height,
                                     std::size_t sample_size) {
    if (width == 0 || height == 0) {
        return core::failure(core::ErrorCode::argument,
                             "A plane needs a positive width and height");
    }
    if (sample_size == 0) {
        return core::failure(core::ErrorCode::invariant, "A sample occupies at least one byte");
    }
    if (width > std::numeric_limits<std::size_t>::max() / sample_size) {
        return std::unexpected(too_large("A row of " + std::to_string(width) + " samples"));
    }
    const auto stride = aligned_up(static_cast<std::size_t>(width) * sample_size);
    if (!stride) {
        return std::unexpected(stride.error());
    }
    return PlaneShape{.width = width, .height = height, .stride = *stride};
}

core::Result<std::size_t> plane_bytes(const PlaneShape& shape) {
    if (shape.height == 0 || shape.stride == 0) {
        return core::failure(core::ErrorCode::argument, "A plane needs a positive height and row");
    }
    if (shape.stride > std::numeric_limits<std::size_t>::max() / shape.height) {
        return std::unexpected(too_large("A plane of " + std::to_string(shape.height) + " rows"));
    }
    return shape.stride * shape.height;
}
} // namespace docenhance::image
