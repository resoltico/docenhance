// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"

#include <cstdint>
#include <span>

namespace docenhance::image {
inline constexpr std::uint32_t linear_block_pixels = 4096;
struct Extent {
    std::uint32_t width{};
    std::uint32_t height{};
    bool operator==(const Extent&) const = default;
};
struct RowRange {
    std::uint32_t row{};
    std::uint32_t first{};
};
// Oriented, opaque linear-light sRGB triplets. Gray is represented by equal RGB channels.
// A block holds at most linear_block_pixels pixels; reads do not quantize or enhance.
// Sources are serialized and immutable in sample meaning; observations count only on output use.
class LinearSource {
  public:
    LinearSource() = default;
    LinearSource(const LinearSource&) = delete;
    LinearSource& operator=(const LinearSource&) = delete;
    LinearSource(LinearSource&&) = delete;
    LinearSource& operator=(LinearSource&&) = delete;
    virtual ~LinearSource() = default;
    [[nodiscard]] virtual Extent extent() const noexcept = 0;
    [[nodiscard]] virtual core::Result<void> read(RowRange range, std::span<double> rgb,
                                                  RowUse use) = 0;
};
// Quantize a block exactly once to the requested gray/RGB byte/word representation.
[[nodiscard]] core::Result<void> quantize_linear(RasterShape output, std::span<const double> rgb,
                                                 std::span<std::uint8_t> bytes);
} // namespace docenhance::image
