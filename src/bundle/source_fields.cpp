// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/bundle/fields.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <variant>
namespace docenhance::bundle {
namespace {
std::string_view color_name(image::JpegColor color) noexcept {
    if (color == image::JpegColor::gray) {
        return "gray";
    }
    return color == image::JpegColor::rgb ? "rgb" : "ycbcr";
}
nlohmann::ordered_json resolution(const std::optional<image::Resolution>& value) {
    return value ? nlohmann::ordered_json{{"x_ppm", value->x}, {"y_ppm", value->y}}
                 : nlohmann::ordered_json(nullptr);
}
} // namespace
nlohmann::ordered_json source_fields(const image::SourceDescription& description) {
    if (const auto* const png = std::get_if<image::PngSource>(&description)) {
        return {
            {"format", "png"},
            {"decoder_policy", "bounded-png-1"},
            {"width", png->width},
            {"height", png->height},
            {"precision", png->depth},
            {"color_type", png->color_type},
            {"interlaced", png->interlaced},
        };
    }
    const auto& jpeg = std::get<image::JpegSource>(description);
    const unsigned components = jpeg.color == image::JpegColor::gray ? 1 : image::jpeg_components;
    nlohmann::ordered_json sampling = nlohmann::ordered_json::array();
    nlohmann::ordered_json identities = nlohmann::ordered_json::array();
    for (unsigned i = 0; i < components; ++i) {
        identities.push_back(jpeg.component_ids.at(i));
        const auto sample = jpeg.sampling.at(i);
        sampling.push_back({
            {"horizontal", sample.horizontal},
            {"vertical", sample.vertical},
        });
    }
    return {
        {"format", "jpeg"},
        {"decoder_policy", "jpeg-islow-fancy-no-smoothing-1"},
        {"width", jpeg.width},
        {"height", jpeg.height},
        {"precision", image::byte_bits},
        {"components", components},
        {"component_ids", identities},
        {"component_interpretation", color_name(jpeg.color)},
        {"coding", "huffman"},
        {"process", jpeg.process == image::JpegProcess::baseline ? "baseline" : "progressive"},
        {"scans", jpeg.scans},
        {"sampling", sampling},
        {
            "adobe_transform",
            jpeg.adobe_transform ? nlohmann::ordered_json(*jpeg.adobe_transform)
                                 : nlohmann::ordered_json(nullptr),
        },
        {"jfif_present", jpeg.jfif_present},
        {"exif_present", jpeg.exif_present},
        {"jfif_resolution", resolution(jpeg.jfif_resolution)},
        {"exif_resolution", resolution(jpeg.exif_resolution)},
        {"resolution_precedence", "exif_then_jfif"},
        {"resolution_conflict", jpeg.resolution_conflict},
    };
}
} // namespace docenhance::bundle
