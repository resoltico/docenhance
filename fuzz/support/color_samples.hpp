// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/color/converter.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "fuzz_input.hpp"
#include "oracle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace docenhance::fuzz {
struct ColorFixture {
    std::uint32_t width;
    std::uint32_t height;
    std::uint8_t orientation;
    unsigned source_depth;
    unsigned output_depth;
    bool gray;
    bool linear;
    bool black;
    std::vector<std::array<std::uint16_t, 4>> pixels;
};
inline double decode_sample(double value, bool linear) {
    if (linear) {
        return value;
    }
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}
inline double encode_sample(double value) {
    return value <= 0.0031308 ? 12.92 * value : (1.055 * std::pow(value, 1.0 / 2.4)) - 0.055;
}
inline std::size_t oriented_index(const ColorFixture& f, std::uint32_t x, std::uint32_t y) {
    const std::array<std::array<std::uint32_t, 2>, 8> points{
        {
            {x, y},
            {f.width - 1 - x, y},
            {f.width - 1 - x, f.height - 1 - y},
            {x, f.height - 1 - y},
            {y, x},
            {y, f.height - 1 - x},
            {f.width - 1 - y, f.height - 1 - x},
            {f.width - 1 - y, x},
        },
    };
    const auto point = points.at(f.orientation - 1);
    return (std::size_t{point.at(1)} * f.width) + point.at(0);
}
inline std::array<std::uint16_t, 3> expected_pixel(const ColorFixture& f, std::size_t index) {
    const auto pixel = f.pixels.at(index);
    const double maximum = f.source_depth == 16 ? 65535 : 255;
    const double alpha = pixel.back() / maximum;
    std::array<double, 3> rgb{};
    for (std::size_t c = 0; c < rgb.size(); ++c) {
        const double encoded = alpha == 0 ? 0 : pixel.at(c) / maximum;
        rgb.at(c) = (alpha * decode_sample(encoded, f.linear)) + ((1 - alpha) * (f.black ? 0 : 1));
    }
    if (f.gray) {
        rgb.front() = (0.2126 * rgb.at(0)) + (0.7152 * rgb.at(1)) + (0.0722 * rgb.at(2));
    }
    std::array<std::uint16_t, 3> result{};
    const double output_max = f.output_depth == 16 ? 65535 : 255;
    for (std::size_t c = 0; c < result.size(); ++c) {
        result.at(c) =
            static_cast<std::uint16_t>(std::floor((output_max * encode_sample(rgb.at(c))) + 0.5));
    }
    return result;
}
inline std::array<std::uint16_t, 4> source_pixel(FuzzInput& input, unsigned depth,
                                                 std::span<std::uint8_t> bytes) {
    std::array<std::uint16_t, 4> pixel{};
    std::size_t at = 0;
    for (auto& sample : pixel) {
        sample = input.integer<std::uint16_t>();
        if (depth == 8) {
            sample &= 255U;
        } else {
            bytes.subspan(at++, 1).front() =
                static_cast<std::uint8_t>(static_cast<unsigned>(sample) >> 8U);
        }
        bytes.subspan(at++, 1).front() = static_cast<std::uint8_t>(sample & 255U);
    }
    return pixel;
}
inline unsigned oracle_sample(std::span<const std::uint8_t> row, std::size_t& at, unsigned depth) {
    unsigned result = row.subspan(at++, 1).front();
    if (depth == 16) {
        result = (result << 8U) | row.subspan(at++, 1).front();
    }
    return result;
}
inline void fill_source(ColorFixture& f, FuzzInput& input, image::Raster& source) {
    for (std::uint32_t y = 0; y < f.height; ++y) {
        const auto row = source.pixels.view().row(y);
        std::size_t at = 0;
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const auto size = 4 * (f.source_depth / 8);
            const auto pixel = source_pixel(input, f.source_depth, row.subspan(at, size));
            at += size;
            f.pixels.push_back(pixel);
        }
    }
}
inline void compare_row(const ColorFixture& f, std::span<const std::uint8_t> row, std::uint32_t y) {
    const unsigned channels = f.gray ? 1 : 3;
    const auto width = f.orientation >= 5 ? f.height : f.width;
    std::size_t at = 0;
    for (std::uint32_t x = 0; x < width; ++x) {
        const auto expected = expected_pixel(f, oriented_index(f, x, y));
        for (unsigned c = 0; c < channels; ++c) {
            const auto actual = oracle_sample(row, at, f.output_depth);
            require(actual == expected.at(c), "independent scalar color sample");
        }
    }
}
inline void compare_rows(const ColorFixture& f, color::Converter& converter, core::Budget& budget) {
    const auto shape = converter.descriptor().shape;
    const auto width = f.orientation >= 5 ? f.height : f.width;
    const auto height = f.orientation >= 5 ? f.width : f.height;
    require(shape.width == width && shape.height == height && shape.depth == f.output_depth,
            "independent oriented color shape");
    const unsigned channels = f.gray ? 1 : 3;
    auto output =
        image::Plane<std::uint8_t>::allocate(budget, width * channels * (f.output_depth / 8), 1)
            .value();
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto row = output.view().row(0);
        require(converter.row(y, row, image::RowUse::output).has_value(),
                "valid structured color row succeeds");
        compare_row(f, row, y);
    }
    const auto maximum = f.source_depth == 16 ? 65535 : 255;
    const auto flattened = static_cast<std::uint64_t>(std::ranges::count_if(
        f.pixels, [maximum](const auto& pixel) { return pixel.back() != maximum; }));
    require(converter.report().flattened_pixels == flattened, "color observations count once");
}
inline void check_color_samples(std::span<const std::uint8_t> bytes) {
    namespace image = docenhance::image;
    namespace color = docenhance::color;
    FuzzInput input{bytes};
    ColorFixture f{
        .width = 1 + (input.byte() % 9U),
        .height = 1 + (input.byte() % 7U),
        .orientation = static_cast<std::uint8_t>(1 + (input.byte() % 8U)),
        .source_depth = input.byte() % 2U == 0 ? 8U : 16U,
        .output_depth = input.byte() % 2U == 0 ? 8U : 16U,
        .gray = input.byte() % 2U != 0,
        .linear = input.byte() % 2U != 0,
        .black = input.byte() % 2U != 0,
        .pixels = {},
    };
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    {
        image::Raster source;
        source.shape = {
            .width = f.width,
            .height = f.height,
            .model = image::SampleModel::rgba,
            .depth = f.source_depth,
        };
        source.metadata.orientation = f.orientation;
        if (f.linear) {
            source.metadata.png()->gamma = 100000;
        }
        source.pixels = image::Plane<std::uint8_t>::allocate(
                            budget, f.width * 4 * (f.source_depth / 8), f.height)
                            .value();
        fill_source(f, input, source);
        const auto operation =
            image::Continuous::create(
                {
                    .mode = f.gray ? image::ToneMode::gray : image::ToneMode::preserve,
                    .depth =
                        f.output_depth == 16 ? image::OutputDepth::word : image::OutputDepth::byte,
                    .alpha = f.black ? image::AlphaPolicy::black : image::AlphaPolicy::white,
                })
                .value();
        auto converter = color::Converter::create(source, operation, budget);
        require(converter.has_value(), "valid structured color source must convert");
        compare_rows(f, **converter, budget);
    }
    require(budget.used() == 0, "structured color conversion refunds every allocation");
}
} // namespace docenhance::fuzz
