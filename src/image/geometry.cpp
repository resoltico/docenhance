// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/geometry.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"

#include <cstdint>
#include <utility>

namespace docenhance::image {
RasterShape rotated_shape(RasterShape shape, QuarterTurn rotation) noexcept {
    if (rotation.swaps_axes()) {
        std::swap(shape.width, shape.height);
    }
    return shape;
}
Coordinate rotation_source_coordinate(RasterShape shape, QuarterTurn rotation,
                                      Coordinate p) noexcept {
    switch (rotation.degrees()) {
    case QuarterTurn::quarter_degrees:
        return {.x = p.y, .y = shape.height - 1 - p.x};
    case QuarterTurn::half_degrees:
        return {.x = shape.width - 1 - p.x, .y = shape.height - 1 - p.y};
    case QuarterTurn::three_quarter_degrees:
        return {.x = shape.width - 1 - p.y, .y = p.x};
    default:
        return p;
    }
}
core::Result<void> rotate_plane(PlaneView<const std::uint8_t> source,
                                PlaneView<std::uint8_t> destination, QuarterTurn rotation,
                                const core::Cancellation& cancellation) {
    const RasterShape before{.width = source.width(), .height = source.height()};
    const auto after = rotated_shape(before, rotation);
    if (source.empty() || destination.empty() || after.width != destination.width() ||
        after.height != destination.height() || overlaps(source, destination)) {
        return core::failure(core::ErrorCode::argument,
                             "Exact rotation requires matching disjoint nonempty planes");
    }
    constexpr std::uint32_t interval = 1024;
    for (std::uint32_t y = 0; y < after.height; ++y) {
        const auto row = destination.row(y);
        for (std::uint32_t x = 0; x < after.width; ++x) {
            if (x % interval == 0 && cancellation.requested(core::Checkpoint::processing)) {
                return core::cancelled();
            }
            const auto p = rotation_source_coordinate(before, rotation, {.x = x, .y = y});
            row.subspan(x, 1).front() = source.row(p.y).subspan(p.x, 1).front();
        }
    }
    return {};
}
} // namespace docenhance::image
