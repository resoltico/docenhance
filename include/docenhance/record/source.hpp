// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/source_facts.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace docenhance::record {
enum class PngDecoderPolicy { bounded };
struct PngContainer {
    PngDecoderPolicy decoder_policy = PngDecoderPolicy::bounded;
    std::uint32_t width{};
    std::uint32_t height{};
    unsigned precision = image::byte_bits;
    unsigned color_type{};
    bool interlaced = false;
    bool operator==(const PngContainer&) const = default;
};
enum class JpegDecoderPolicy { bounded };
enum class JpegCoding { huffman };
enum class ResolutionPrecedence { exif_then_jfif };
struct JpegContainer {
    JpegDecoderPolicy decoder_policy = JpegDecoderPolicy::bounded;
    std::uint32_t width{};
    std::uint32_t height{};
    unsigned precision = image::byte_bits;
    unsigned components = 1;
    std::vector<std::uint8_t> component_ids;
    image::JpegColor component_interpretation = image::JpegColor::gray;
    JpegCoding coding = JpegCoding::huffman;
    image::JpegProcess process = image::JpegProcess::baseline;
    unsigned scans = 1;
    std::vector<image::Sampling> sampling;
    std::optional<unsigned> adobe_transform;
    bool jfif_present = false;
    bool exif_present = false;
    std::optional<image::Resolution> jfif_resolution;
    std::optional<image::Resolution> exif_resolution;
    ResolutionPrecedence resolution_precedence = ResolutionPrecedence::exif_then_jfif;
    bool resolution_conflict = false;
    bool operator==(const JpegContainer&) const = default;
};
enum class TiffDecoderPolicy { bounded };
struct TiffContainer {
    TiffDecoderPolicy decoder_policy = TiffDecoderPolicy::bounded;
    std::uint32_t width{};
    std::uint32_t height{};
    unsigned precision = image::byte_bits;
    unsigned samples = 1;
    unsigned photometric = 1;
    unsigned compression = 1;
    unsigned planar = 1;
    unsigned predictor = 1;
    unsigned alpha{};
    bool bigtiff = false;
    bool tiled = false;
    unsigned pages = 1;
    image::Orientation orientation;
    std::optional<image::Resolution> resolution;
    bool operator==(const TiffContainer&) const = default;
};
using SourceContainer = std::variant<PngContainer, JpegContainer, TiffContainer>;
[[nodiscard]] core::Result<SourceContainer> source_container(const image::SourceFacts& value);
[[nodiscard]] std::optional<image::SourceFacts>
decoded_source(const SourceContainer& value) noexcept;
[[nodiscard]] bool validate(const PngContainer& value) noexcept;
[[nodiscard]] bool validate(const JpegContainer& value) noexcept;
[[nodiscard]] bool validate(const TiffContainer& value) noexcept;
} // namespace docenhance::record
