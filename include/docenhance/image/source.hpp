// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/raster.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <variant>
namespace docenhance::image {
inline constexpr unsigned jpeg_components = 3;
inline constexpr unsigned jpeg_sampling_max = 4;
inline constexpr unsigned jpeg_mcu_blocks_max = 10;
inline constexpr unsigned jpeg_dimension_max = 65500;
inline constexpr unsigned jpeg_scan_max = 128;
inline constexpr std::uint64_t source_encoded_bytes_max = std::uint64_t{128} * 1024 * 1024;
inline constexpr std::uint64_t source_pixels_max = 40'000'000;
inline constexpr unsigned png_nibble_depth = 4;
inline constexpr unsigned png_palette = 3;
inline constexpr unsigned png_gray_alpha = 4;
inline constexpr unsigned png_rgb_alpha = 6;
struct PngSource {
    std::uint32_t width{};
    std::uint32_t height{};
    unsigned depth = byte_bits;
    unsigned color_type{};
    bool interlaced = false;
};
enum class JpegProcess { baseline, progressive };
struct Sampling {
    unsigned horizontal = 1;
    unsigned vertical = 1;
};
struct JpegSource {
    std::uint32_t width{};
    std::uint32_t height{};
    JpegColor color = JpegColor::gray;
    JpegProcess process = JpegProcess::baseline;
    unsigned scans{};
    std::array<unsigned, jpeg_components> component_ids{};
    std::array<Sampling, jpeg_components> sampling{};
    std::optional<unsigned> adobe_transform;
    std::optional<Resolution> jfif_resolution;
    std::optional<Resolution> exif_resolution;
    bool jfif_present = false;
    bool exif_present = false;
    bool resolution_conflict = false;
};
using SourceDescription = std::variant<PngSource, JpegSource>;
[[nodiscard]] bool valid_source_description(const SourceDescription& description) noexcept;
} // namespace docenhance::image
