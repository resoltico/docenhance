// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/request.hpp"
#include "docenhance/record/source.hpp"
#include "validation.hpp"

#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::record {
namespace {
bool png_agrees(const image::PngFacts& png, const image::InterpretationReport& report) {
    const auto decoded = report.decoded;
    const unsigned depth = png.depth == image::word_bits ? image::word_bits : image::byte_bits;
    const bool color = png.color_type == 2 ||
                       png.color_type == static_cast<unsigned>(image::PngColor::palette) ||
                       png.color_type == static_cast<unsigned>(image::PngColor::rgb_alpha);
    const bool explicit_alpha =
        png.color_type == static_cast<unsigned>(image::PngColor::gray_alpha) ||
        png.color_type == static_cast<unsigned>(image::PngColor::rgb_alpha);
    return png.width == decoded.width && png.height == decoded.height &&
           depth == decoded.depth.bits() && color == image::is_color(decoded.model) &&
           (!explicit_alpha || image::has_alpha(decoded.model));
}
bool native_profile(image::Interpretation value) {
    return value == image::Interpretation::assumed_srgb ||
           value == image::Interpretation::overridden_srgb || value == image::Interpretation::icc;
}
bool jpeg_agrees(const image::JpegFacts& jpeg, const image::InterpretationReport& report) {
    const auto decoded = report.decoded;
    auto resolution = jpeg.exif_resolution ? jpeg.exif_resolution : jpeg.jfif_resolution;
    if (resolution && (report.orientation.transposed() != report.rotation.swaps_axes())) {
        std::swap(resolution->x, resolution->y);
    }
    return jpeg.width == decoded.width && jpeg.height == decoded.height &&
           decoded.depth == image::SampleDepth::byte() &&
           decoded.model == (jpeg.color == image::JpegColor::gray ? image::SampleModel::gray
                                                                  : image::SampleModel::rgb) &&
           (jpeg.exif_present || report.orientation == image::Orientation{}) &&
           resolution == report.resolution && native_profile(report.interpretation);
}
bool tiff_agrees(const image::TiffFacts& tiff, const image::InterpretationReport& report) {
    auto resolution = tiff.resolution;
    if (resolution && (tiff.orientation.transposed() != report.rotation.swaps_axes())) {
        std::swap(resolution->x, resolution->y);
    }
    return report.decoded == image::decoded_tiff_shape(tiff) &&
           report.orientation == tiff.orientation && report.resolution == resolution &&
           native_profile(report.interpretation);
}
template <typename Output>
bool continuous_output_agrees(const Output& output, const RunRecord& value) {
    const auto& report = value.execution.interpretation;
    const auto model =
        std::is_same_v<Output, GrayOutputRequest> || !image::is_color(report.decoded.model)
            ? image::SampleModel::gray
            : image::SampleModel::rgb;
    unsigned depth = report.decoded.depth.bits();
    if (output.depth == image::OutputDepth::byte) {
        depth = image::byte_bits;
    }
    if (output.depth == image::OutputDepth::word) {
        depth = image::word_bits;
    }
    return value.output.channels == model && value.output.depth.bits() == depth;
}
bool output_agrees(const RunRecord& value) {
    const auto& report = value.execution.interpretation;
    if (report.output != value.output.shape() || report.resolution != value.output.resolution ||
        report.rotation != value.request.rotation) {
        return false;
    }
    return std::visit(
        [&](const auto& output) {
            if (output.alpha == image::AlphaPolicy::reject && report.flattened_pixels != 0) {
                return false;
            }
            if ((output.profile_policy == image::ProfilePolicy::srgb) !=
                (report.interpretation == image::Interpretation::overridden_srgb)) {
                return false;
            }
            using Output = std::remove_cvref_t<decltype(output)>;
            if constexpr (std::is_same_v<Output, BinaryOutputRequest>) {
                return value.output.depth.bits() == 1;
            } else {
                return continuous_output_agrees(output, value);
            }
        },
        value.request.output);
}
} // namespace
bool source_agrees(const RunRecord& value) {
    const auto source = decoded_source(value.source.container);
    if (!source || !output_agrees(value)) {
        return false;
    }
    const auto& report = value.execution.interpretation;
    if (const auto* const png = std::get_if<image::PngFacts>(&*source)) {
        return png_agrees(*png, report);
    }
    if (const auto* const jpeg = std::get_if<image::JpegFacts>(&*source)) {
        return jpeg_agrees(*jpeg, report);
    }
    return tiff_agrees(std::get<image::TiffFacts>(*source), report);
}
} // namespace docenhance::record
