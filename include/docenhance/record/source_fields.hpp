// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/record/source.hpp"

namespace docenhance {
template <> struct core::EnumNames<record::PngDecoderPolicy> {
    static constexpr auto list = std::to_array({
        std::pair{record::PngDecoderPolicy::bounded, std::string_view{"bounded-png-1"}},
    });
};
template <> struct core::WireTag<record::PngContainer> {
    static constexpr std::string_view key = "format";
    static constexpr std::string_view value = "png";
};
template <> struct core::Fields<record::PngContainer> {
    using Value = record::PngContainer;
    static constexpr auto list = std::tuple{
        core::field("decoder_policy", &Value::decoder_policy),
        core::field("width", &Value::width, core::Count{}),
        core::field("height", &Value::height, core::Count{}),
        core::field("precision", &Value::precision, core::Choices{std::array{1U, 2U, 4U, 8U, 16U}}),
        core::field("color_type", &Value::color_type,
                    core::Choices{std::array{0U, 2U, 3U, 4U, 6U}}),
        core::field("interlaced", &Value::interlaced),
    };
};
template <> struct core::EnumNames<record::JpegDecoderPolicy> {
    static constexpr auto list = std::to_array({
        std::pair{record::JpegDecoderPolicy::bounded,
                  std::string_view{"jpeg-islow-fancy-no-smoothing-1"}},
    });
};
template <> struct core::EnumNames<record::JpegCoding> {
    static constexpr auto list = std::to_array({
        std::pair{record::JpegCoding::huffman, std::string_view{"huffman"}},
    });
};
template <> struct core::EnumNames<record::ResolutionPrecedence> {
    static constexpr auto list = std::to_array({
        std::pair{record::ResolutionPrecedence::exif_then_jfif, std::string_view{"exif_then_jfif"}},
    });
};
template <> struct core::EnumNames<record::TiffDecoderPolicy> {
    static constexpr auto list = std::to_array({
        std::pair{record::TiffDecoderPolicy::bounded, std::string_view{"bounded-tiff-striles-1"}},
    });
};
template <> struct core::EnumNames<image::JpegColor> {
    static constexpr std::array list{
        std::pair{image::JpegColor::gray, std::string_view{"gray"}},
        std::pair{image::JpegColor::rgb, std::string_view{"rgb"}},
        std::pair{image::JpegColor::ycbcr, std::string_view{"ycbcr"}},
    };
};
template <> struct core::EnumNames<image::JpegProcess> {
    static constexpr std::array list{
        std::pair{image::JpegProcess::baseline, std::string_view{"baseline"}},
        std::pair{image::JpegProcess::progressive, std::string_view{"progressive"}},
    };
};
template <> struct core::WireTag<record::JpegContainer> {
    static constexpr std::string_view key = "format";
    static constexpr std::string_view value = "jpeg";
};
template <> struct core::WireTag<record::TiffContainer> {
    static constexpr std::string_view key = "format";
    static constexpr std::string_view value = "tiff";
};
template <> struct core::Fields<image::Sampling> {
    static constexpr auto list = std::tuple{
        core::field("horizontal", &image::Sampling::horizontal,
                    core::Range<unsigned>{.minimum = 1, .maximum = image::jpeg_sampling_max}),
        core::field("vertical", &image::Sampling::vertical,
                    core::Range<unsigned>{.minimum = 1, .maximum = image::jpeg_sampling_max}),
    };
};
template <> struct core::Fields<record::JpegContainer> {
    using Value = record::JpegContainer;
    static constexpr auto dimension =
        core::Range<std::uint32_t>{.minimum = 1, .maximum = image::jpeg_dimension_max};
    static constexpr auto components =
        core::Range<std::size_t>{.minimum = 1, .maximum = image::jpeg_components};
    static constexpr auto list = std::tuple{
        core::field("decoder_policy", &Value::decoder_policy),
        core::field("width", &Value::width, dimension),
        core::field("height", &Value::height, dimension),
        core::field("precision", &Value::precision, core::Exact{image::byte_bits}),
        core::field("components", &Value::components, core::Choices{std::array{1U, 3U}}),
        core::field("component_ids", &Value::component_ids, components),
        core::field("component_interpretation", &Value::component_interpretation),
        core::field("coding", &Value::coding),
        core::field("process", &Value::process),
        core::field("scans", &Value::scans,
                    core::Range<unsigned>{.minimum = 1, .maximum = image::jpeg_scan_max}),
        core::field("sampling", &Value::sampling, components),
        core::field("adobe_transform", &Value::adobe_transform,
                    core::Range<unsigned>{.minimum = 0, .maximum = 1}),
        core::field("jfif_present", &Value::jfif_present),
        core::field("exif_present", &Value::exif_present),
        core::field("jfif_resolution", &Value::jfif_resolution),
        core::field("exif_resolution", &Value::exif_resolution),
        core::field("resolution_precedence", &Value::resolution_precedence),
        core::field("resolution_conflict", &Value::resolution_conflict),
    };
};
template <> struct core::Fields<record::TiffContainer> {
    using Value = record::TiffContainer;
    static constexpr auto list = std::tuple{
        core::field("decoder_policy", &Value::decoder_policy),
        core::field("width", &Value::width, core::Count{}),
        core::field("height", &Value::height, core::Count{}),
        core::field("precision", &Value::precision, core::Choices{std::array{1U, 8U, 16U}}),
        core::field("samples", &Value::samples,
                    core::Range<unsigned>{.minimum = 1, .maximum = image::rgba_channels}),
        core::field("photometric", &Value::photometric,
                    core::Choices{std::array{0U, 1U, 2U, 3U, 6U}}),
        core::field("compression", &Value::compression,
                    core::Choices{std::array{1U, 3U, 4U, 5U, 7U, 8U, 32773U, 32946U}}),
        core::field("planar", &Value::planar, core::Range<unsigned>{.minimum = 1, .maximum = 2}),
        core::field("predictor", &Value::predictor,
                    core::Range<unsigned>{.minimum = 1, .maximum = 2}),
        core::field("alpha", &Value::alpha, core::Range<unsigned>{.minimum = 0, .maximum = 2}),
        core::field("bigtiff", &Value::bigtiff),
        core::field("tiled", &Value::tiled),
        core::field("pages", &Value::pages, core::Exact{1U}),
        core::field("orientation", &Value::orientation),
        core::field("resolution", &Value::resolution),
    };
};
} // namespace docenhance
