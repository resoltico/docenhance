// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"

#include <cstdint>
#include <span>
namespace docenhance::io {
inline constexpr std::size_t tiff_native_payload_max = std::size_t{32} * 1024 * 1024;
inline constexpr std::size_t tiff_control_reservation = std::size_t{1} * 1024 * 1024;
inline constexpr std::size_t tiff_decoded_unit_max = std::size_t{8} * 1024 * 1024;
inline constexpr std::size_t tiff_encoded_unit_max = std::size_t{16} * 1024 * 1024;
inline constexpr unsigned tiff_units_max = 65536;
struct TiffLimits {
    std::uint64_t encoded_bytes = image::source_encoded_bytes_max;
    std::uint64_t pixels = image::source_pixels_max;
};
[[nodiscard]] bool has_tiff_signature(std::span<const std::uint8_t> bytes) noexcept;
struct DecodedTiff {
    image::Raster raster;
    image::TiffSource source;
};
[[nodiscard]] core::Result<DecodedTiff>
decode_tiff(std::span<const std::uint8_t> bytes, core::Budget& budget, image::ProfilePolicy profile,
            const core::Cancellation& cancellation = {}, TiffLimits limits = {});
} // namespace docenhance::io
