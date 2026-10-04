// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/jpeg.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace docenhance::io {
struct JpegScan {
    image::JpegSource source;
    image::RasterMetadata metadata;
};
struct JpegMarkers {
    JpegScan scan;
    std::array<std::span<const std::uint8_t>, image::byte_max> icc;
    std::span<const std::uint8_t> exif;
    unsigned components{};
    unsigned icc_count{};
    std::size_t profile_bytes{};
    std::size_t marker_bytes{};
    bool framed = false;
};
[[nodiscard]] unsigned jpeg_word(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] core::Result<void> jpeg_metadata(JpegMarkers& state, unsigned marker,
                                               std::span<const std::uint8_t> bytes);
[[nodiscard]] core::Result<void> finish_jpeg_metadata(JpegMarkers& state, core::Budget& budget,
                                                      image::ProfilePolicy policy,
                                                      const core::Cancellation& cancellation);
[[nodiscard]] core::Result<JpegScan> scan_jpeg(std::span<const std::uint8_t> bytes,
                                               core::Budget& budget, image::ProfilePolicy policy,
                                               const core::Cancellation& cancellation,
                                               JpegLimits limits);
} // namespace docenhance::io
