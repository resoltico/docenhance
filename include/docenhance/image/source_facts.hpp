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
// Shared fact-domain bounds for codecs, independent image validation and record admission.
inline constexpr unsigned jpeg_sampling_max = 4;
inline constexpr unsigned jpeg_mcu_blocks_max = 10;
inline constexpr unsigned jpeg_dimension_max = 65500;
inline constexpr unsigned jpeg_scan_max = 128;
inline constexpr std::uint64_t source_encoded_bytes_max = std::uint64_t{128} * 1024 * 1024;
inline constexpr std::uint64_t source_pixels_max = 40'000'000;
// Container codes are shared fact vocabulary; decoder-internal tags stay in codec.
enum class PngColor : unsigned { gray = 0, rgb = 2, palette = 3, gray_alpha = 4, rgb_alpha = 6 };
struct PngFacts {
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
struct JpegFacts {
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
enum class TiffPhotometric : unsigned {
    miniswhite = 0,
    minisblack = 1,
    rgb = 2,
    palette = 3,
    ycbcr = 6,
};
enum class TiffCompression : unsigned {
    none = 1,
    ccitt_group3 = 3,
    ccitt_group4 = 4,
    lzw = 5,
    jpeg = 7,
    deflate = 8,
    packbits = 32773,
    adobe_deflate = 32946,
};
struct TiffFacts {
    std::uint32_t width{};
    std::uint32_t height{};
    unsigned depth = byte_bits;
    unsigned samples = 1;
    unsigned photometric = 1;
    unsigned compression = 1;
    unsigned planar = 1;
    unsigned predictor = 1;
    unsigned alpha{};
    bool big = false;
    bool tiled = false;
    Orientation orientation;
    std::optional<Resolution> resolution;
};
[[nodiscard]] RasterShape decoded_tiff_shape(const TiffFacts& source) noexcept;
[[nodiscard]] bool valid_tiff_facts(const TiffFacts& source) noexcept;
using SourceFacts = std::variant<PngFacts, JpegFacts, TiffFacts>;
[[nodiscard]] bool valid_source_facts(const SourceFacts& description) noexcept;
} // namespace docenhance::image
