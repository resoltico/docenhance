// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/binarization.hpp"
#include "read_fields.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
#include <variant>
namespace docenhance::bundle {
namespace {
unsigned integer(const RecordJson& value, unsigned maximum) {
    return static_cast<unsigned>(record_integer(value, maximum));
}
core::Result<image::SourceDescription> invalid() {
    return core::failure(core::ErrorCode::input, "Invalid source decoding observations");
}
core::Result<image::SourceDescription> jpeg_source(const RecordJson& value) {
    const auto color = record_text(record_field(value, "component_interpretation"));
    const auto process = record_text(record_field(value, "process"));
    if ((color != "gray" && color != "rgb" && color != "ycbcr") ||
        (process != "baseline" && process != "progressive")) {
        return invalid();
    }
    image::JpegColor selected = image::JpegColor::ycbcr;
    if (color == "gray") {
        selected = image::JpegColor::gray;
    }
    if (color == "rgb") {
        selected = image::JpegColor::rgb;
    }
    image::JpegSource jpeg{
        .width = integer(record_field(value, "width"), image::jpeg_dimension_max),
        .height = integer(record_field(value, "height"), image::jpeg_dimension_max),
        .color = selected,
        .process =
            process == "baseline" ? image::JpegProcess::baseline : image::JpegProcess::progressive,
        .scans = integer(record_field(value, "scans"), image::jpeg_scan_max),
        .adobe_transform = std::nullopt,
        .jfif_resolution = record_resolution(record_field(value, "jfif_resolution")),
        .exif_resolution = record_resolution(record_field(value, "exif_resolution")),
        .jfif_present = record_boolean(record_field(value, "jfif_present")),
        .exif_present = record_boolean(record_field(value, "exif_present")),
        .resolution_conflict = record_boolean(record_field(value, "resolution_conflict")),
    };
    const auto& adobe = record_field(value, "adobe_transform");
    if (!adobe.is_null()) {
        jpeg.adobe_transform = integer(adobe, 1);
    }
    const auto& sampling = record_field(value, "sampling");
    const auto& identities = record_field(value, "component_ids");
    const unsigned components = jpeg.color == image::JpegColor::gray ? 1 : image::jpeg_components;
    if (!sampling.is_array() || sampling.size() != components || !identities.is_array() ||
        identities.size() != components) {
        return invalid();
    }
    unsigned id_index = 0;
    for (const auto& identity : identities) {
        jpeg.component_ids.at(id_index++) = integer(identity, image::byte_max);
    }
    unsigned index = 0;
    for (const auto& sample : sampling) {
        jpeg.sampling.at(index++) = {
            .horizontal = integer(record_field(sample, "horizontal"), image::jpeg_sampling_max),
            .vertical = integer(record_field(sample, "vertical"), image::jpeg_sampling_max),
        };
    }
    return image::SourceDescription{jpeg};
}
} // namespace
core::Result<image::SourceDescription> record_source(const RecordJson& value) {
    const auto format = record_text(record_field(value, "format"));
    core::Result<image::SourceDescription> result = invalid();
    if (format == "png") {
        result = image::PngSource{
            .width = integer(record_field(value, "width"), UINT32_MAX),
            .height = integer(record_field(value, "height"), UINT32_MAX),
            .depth = integer(record_field(value, "precision"), image::word_bits),
            .color_type = integer(record_field(value, "color_type"), image::png_rgb_alpha),
            .interlaced = record_boolean(record_field(value, "interlaced")),
        };
    } else if (format == "tiff") {
        const auto orientation =
            image::Orientation::from_code(integer(record_field(value, "orientation"), 8));
        if (!orientation) {
            return invalid();
        }
        result = image::TiffSource{
            .width = integer(record_field(value, "width"), UINT32_MAX),
            .height = integer(record_field(value, "height"), UINT32_MAX),
            .depth = integer(record_field(value, "precision"), image::word_bits),
            .samples = integer(record_field(value, "samples"), image::rgba_channels),
            .photometric = integer(record_field(value, "photometric"), image::tiff_ycbcr),
            .compression = integer(record_field(value, "compression"), image::tiff_adobe_deflate),
            .planar = integer(record_field(value, "planar"), 2),
            .predictor = integer(record_field(value, "predictor"), 2),
            .alpha = integer(record_field(value, "alpha"), 2),
            .big = record_boolean(record_field(value, "bigtiff")),
            .tiled = record_boolean(record_field(value, "tiled")),
            .orientation = *orientation,
            .resolution = record_resolution(record_field(value, "resolution")),
        };
    } else if (format == "jpeg") {
        result = jpeg_source(value);
    }
    if (!result || !image::valid_source_description(*result)) {
        return invalid();
    }
    return result;
}
core::Result<SourceFacts> record_source_facts(const RecordJson& value) {
    SourceFacts facts{
        .identity = record_identity(value),
        .name = record_text(record_field(value, "name")),
        .decoding = std::nullopt,
    };
    auto decoding = record_source(record_field(value, "decoding"));
    if (!decoding) {
        return std::unexpected(decoding.error());
    }
    facts.decoding = *decoding;
    return facts;
}
namespace {
bool png_agrees(const image::PngSource& png, const image::ConversionReport& conversion) {
    const auto decoded = conversion.source;

    const unsigned depth = png.depth == image::word_bits ? image::word_bits : image::byte_bits;
    const bool color = png.color_type == 2 || png.color_type == image::png_palette ||
                       png.color_type == image::png_rgb_alpha;
    return png.width == decoded.width && png.height == decoded.height &&
           depth == decoded.depth.bits() && color == image::is_color(decoded.model) &&
           ((png.color_type != image::png_gray_alpha && png.color_type != image::png_rgb_alpha) ||
            image::has_alpha(decoded.model));
}
bool jpeg_agrees(const image::JpegSource& jpeg, const image::ConversionReport& conversion) {
    const auto decoded = conversion.source;
    auto resolution = jpeg.exif_resolution ? jpeg.exif_resolution : jpeg.jfif_resolution;
    if (resolution && conversion.orientation.transposed()) {
        std::swap(resolution->x, resolution->y);
    }
    const bool interpretation =
        conversion.interpretation == image::Interpretation::assumed_srgb ||
        conversion.interpretation == image::Interpretation::overridden_srgb ||
        conversion.interpretation == image::Interpretation::icc;
    return jpeg.width == decoded.width && jpeg.height == decoded.height &&
           decoded.depth == image::SampleDepth::byte() &&
           decoded.model == (jpeg.color == image::JpegColor::gray ? image::SampleModel::gray
                                                                  : image::SampleModel::rgb) &&
           (jpeg.exif_present || conversion.orientation == image::Orientation{}) &&
           resolution == conversion.resolution && interpretation;
}
} // namespace
bool source_agrees(const DeclaredBundle& declared) {
    if (!declared.operation) {
        return false;
    }
    if (!declared.source.decoding) {
        return false;
    }
    const auto& source = *declared.source.decoding;
    if (declared.source.identity.bytes > image::source_encoded_bytes_max) {
        return false;
    }
    if (!image::valid_source_description(source)) {
        return false;
    }
    const auto* const png = std::get_if<image::PngSource>(&source);
    if (std::holds_alternative<methods::Binarization>(*declared.operation)) {
        return png != nullptr && png->color_type == 0 && png->depth <= image::byte_bits &&
               png->width == declared.output.shape.width &&
               png->height == declared.output.shape.height;
    }
    if (!declared.conversion) {
        return false;
    }
    const auto& conversion = *declared.conversion;
    if (!conversion.verified) {
        return false;
    }
    if (png != nullptr) {
        return png_agrees(*png, conversion);
    }
    if (const auto* const tiff = std::get_if<image::TiffSource>(&source)) {
        auto resolution = tiff->resolution;
        if (resolution && tiff->orientation.transposed()) {
            std::swap(resolution->x, resolution->y);
        }
        const bool interpretation =
            conversion.interpretation == image::Interpretation::assumed_srgb ||
            conversion.interpretation == image::Interpretation::overridden_srgb ||
            conversion.interpretation == image::Interpretation::icc;
        return conversion.source == image::decoded_tiff_shape(*tiff) &&
               conversion.orientation == tiff->orientation && conversion.resolution == resolution &&
               interpretation;
    }
    return jpeg_agrees(std::get<image::JpegSource>(source), conversion);
}
} // namespace docenhance::bundle
