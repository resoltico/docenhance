// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"

#include <cstdint>
#include <optional>

namespace docenhance::image {
// A clockwise exact permutation, independently admitted from metadata orientation.
class QuarterTurn {
  public:
    static constexpr unsigned quarter_degrees = 90;
    static constexpr unsigned half_degrees = 180;
    static constexpr unsigned three_quarter_degrees = 270;
    constexpr QuarterTurn() noexcept = default;
    [[nodiscard]] static constexpr QuarterTurn identity() noexcept {
        return {};
    }
    [[nodiscard]] static constexpr std::optional<QuarterTurn>
    from_degrees(unsigned degrees) noexcept {
        if (degrees != 0 && degrees != quarter_degrees && degrees != half_degrees &&
            degrees != three_quarter_degrees) {
            return std::nullopt;
        }
        return QuarterTurn{degrees};
    }
    [[nodiscard]] constexpr unsigned degrees() const noexcept {
        return degrees_;
    }
    [[nodiscard]] constexpr bool swaps_axes() const noexcept {
        return degrees_ == quarter_degrees || degrees_ == three_quarter_degrees;
    }
    constexpr bool operator==(const QuarterTurn&) const = default;

  private:
    explicit constexpr QuarterTurn(unsigned degrees) noexcept : degrees_(degrees) {}
    unsigned degrees_ = 0;
};
[[nodiscard]] RasterShape rotated_shape(RasterShape shape, QuarterTurn rotation) noexcept;
// Inverse C -> B pixel-center map. p must lie inside rotated_shape(shape, rotation).
[[nodiscard]] Coordinate rotation_source_coordinate(RasterShape shape, QuarterTurn rotation,
                                                    Coordinate p) noexcept;
// The caller owns both disjoint planes. No interpolation, thresholding or allocation occurs.
[[nodiscard]] core::Result<void> rotate_plane(PlaneView<const std::uint8_t> source,
                                              PlaneView<std::uint8_t> destination,
                                              QuarterTurn rotation,
                                              const core::Cancellation& cancellation = {});
} // namespace docenhance::image
