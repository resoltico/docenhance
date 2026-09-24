// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/png.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace docenhance::io {
inline constexpr std::size_t png_signature_bytes = 8;
inline constexpr std::size_t png_integer_bytes = 4;
inline constexpr std::size_t png_chunk_overhead = 12;
inline constexpr std::uint32_t chunk_ihdr = 0x49484452;
inline constexpr std::uint32_t chunk_plte = 0x504c5445;
inline constexpr std::uint32_t chunk_idat = 0x49444154;
inline constexpr std::uint32_t chunk_iend = 0x49454e44;
inline constexpr std::uint32_t chunk_trns = 0x74524e53;
[[nodiscard]] std::uint32_t png_integer(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] bool pixel_chunk(std::uint32_t type) noexcept;
struct PngScan {
    image::RasterShape shape;
    image::PngMetadata metadata;
};
[[nodiscard]] core::Result<PngScan> scan_png(std::span<const std::uint8_t> bytes,
                                             core::Budget& budget, image::ProfilePolicy policy,
                                             const core::Cancellation& cancellation,
                                             PngLimits limits);
[[nodiscard]] core::Result<core::Buffer> inflate_profile(std::span<const std::uint8_t> bytes,
                                                         core::Budget& budget);
[[nodiscard]] core::Result<void> parse_exif(std::span<const std::uint8_t> bytes,
                                            image::PngMetadata& metadata);
} // namespace docenhance::io
