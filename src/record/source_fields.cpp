// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/record/source.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
namespace docenhance::record {
namespace {
image::PngFacts decoding(const PngContainer& value) noexcept {
    return {
        .width = value.width,
        .height = value.height,
        .depth = value.precision,
        .color_type = value.color_type,
        .interlaced = value.interlaced,
    };
}
image::TiffFacts decoding(const TiffContainer& value) noexcept {
    return {
        .width = value.width,
        .height = value.height,
        .depth = value.precision,
        .samples = value.samples,
        .photometric = value.photometric,
        .compression = value.compression,
        .planar = value.planar,
        .predictor = value.predictor,
        .alpha = value.alpha,
        .big = value.bigtiff,
        .tiled = value.tiled,
        .orientation = value.orientation,
        .resolution = value.resolution,
    };
}
std::optional<image::JpegFacts> decoding(const JpegContainer& value) noexcept {
    const unsigned components =
        value.component_interpretation == image::JpegColor::gray ? 1 : image::jpeg_components;
    if (value.components != components || value.component_ids.size() != components ||
        value.sampling.size() != components) {
        return std::nullopt;
    }
    image::JpegFacts result{
        .width = value.width,
        .height = value.height,
        .color = value.component_interpretation,
        .process = value.process,
        .scans = value.scans,
        .adobe_transform = value.adobe_transform,
        .jfif_resolution = value.jfif_resolution,
        .exif_resolution = value.exif_resolution,
        .jfif_present = value.jfif_present,
        .exif_present = value.exif_present,
        .resolution_conflict = value.resolution_conflict,
    };
    std::ranges::copy(value.component_ids, result.component_ids.begin());
    std::ranges::copy(value.sampling, result.sampling.begin());
    return result;
}
PngContainer container(const image::PngFacts& value) {
    return {
        .width = value.width,
        .height = value.height,
        .precision = value.depth,
        .color_type = value.color_type,
        .interlaced = value.interlaced,
    };
}
TiffContainer container(const image::TiffFacts& value) {
    return {
        .width = value.width,
        .height = value.height,
        .precision = value.depth,
        .samples = value.samples,
        .photometric = value.photometric,
        .compression = value.compression,
        .planar = value.planar,
        .predictor = value.predictor,
        .alpha = value.alpha,
        .bigtiff = value.big,
        .tiled = value.tiled,
        .orientation = value.orientation,
        .resolution = value.resolution,
    };
}
JpegContainer container(const image::JpegFacts& value) {
    const unsigned components = value.color == image::JpegColor::gray ? 1 : image::jpeg_components;
    JpegContainer result{
        .width = value.width,
        .height = value.height,
        .components = components,
        .component_ids = {},
        .component_interpretation = value.color,
        .process = value.process,
        .scans = value.scans,
        .sampling = {},
        .adobe_transform = value.adobe_transform,
        .jfif_present = value.jfif_present,
        .exif_present = value.exif_present,
        .jfif_resolution = value.jfif_resolution,
        .exif_resolution = value.exif_resolution,
        .resolution_conflict = value.resolution_conflict,
    };
    for (std::size_t i = 0; i < components; ++i) {
        result.component_ids.push_back(static_cast<std::uint8_t>(value.component_ids.at(i)));
        result.sampling.push_back(value.sampling.at(i));
    }
    return result;
}
} // namespace
bool validate(const PngContainer& value) noexcept {
    return value.decoder_policy == PngDecoderPolicy::bounded &&
           image::valid_source_facts(decoding(value));
}
bool validate(const JpegContainer& value) noexcept {
    const auto facts = decoding(value);
    return value.decoder_policy == JpegDecoderPolicy::bounded &&
           value.precision == image::byte_bits && value.coding == JpegCoding::huffman &&
           value.resolution_precedence == ResolutionPrecedence::exif_then_jfif && facts &&
           image::valid_source_facts(*facts);
}
bool validate(const TiffContainer& value) noexcept {
    return value.decoder_policy == TiffDecoderPolicy::bounded && value.pages == 1 &&
           image::valid_source_facts(decoding(value));
}
core::Result<SourceContainer> source_container(const image::SourceFacts& value) {
    if (!image::valid_source_facts(value)) {
        return core::failure(core::ErrorCode::invariant, "Invalid source container facts");
    }
    return std::visit([](const auto& facts) -> SourceContainer { return container(facts); }, value);
}
std::optional<image::SourceFacts> decoded_source(const SourceContainer& value) noexcept {
    if (const auto* const png = std::get_if<PngContainer>(&value)) {
        return validate(*png) ? std::optional<image::SourceFacts>{decoding(*png)} : std::nullopt;
    }
    if (const auto* const jpeg = std::get_if<JpegContainer>(&value)) {
        if (!validate(*jpeg)) {
            return std::nullopt;
        }
        const auto facts = decoding(*jpeg);
        return facts ? std::optional<image::SourceFacts>{*facts} : std::nullopt;
    }
    if (const auto* const tiff = std::get_if<TiffContainer>(&value)) {
        return validate(*tiff) ? std::optional<image::SourceFacts>{decoding(*tiff)} : std::nullopt;
    }
    return std::nullopt;
}
} // namespace docenhance::record
