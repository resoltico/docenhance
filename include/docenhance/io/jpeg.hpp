// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
namespace docenhance::io {
inline constexpr std::size_t jpeg_max_encoded_bytes = image::source_encoded_bytes_max;
inline constexpr std::uint64_t jpeg_max_pixels = image::source_pixels_max;
inline constexpr unsigned jpeg_max_scans = image::jpeg_scan_max;
inline constexpr std::size_t jpeg_max_markers = 65536;
struct JpegLimits {
    std::size_t encoded_bytes = jpeg_max_encoded_bytes;
    std::uint64_t pixels = jpeg_max_pixels;
    unsigned scans = jpeg_max_scans;
};
struct DecodedJpeg {
    image::Raster raster;
    image::JpegSource source;
    std::size_t decoder_charge_peak{};
    std::size_t native_block_peak{};
    std::size_t native_allocations{};
    std::size_t working_charge_peak{};
};
[[nodiscard]] core::Result<DecodedJpeg>
decode_jpeg(std::span<const std::uint8_t> bytes, core::Budget& budget, image::ProfilePolicy profile,
            const core::Cancellation& cancellation = {}, JpegLimits limits = {});
} // namespace docenhance::io
