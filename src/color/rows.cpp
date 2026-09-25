// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "context.hpp"
#include "conversion.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <lcms2.h>
#include <span>

namespace docenhance::color {
namespace {
struct RowPart {
    std::uint32_t y{};
    std::uint32_t first{};
    std::uint32_t count{};
};
core::Result<void> gather(ConversionState& state, RowPart part, image::RowUse use) {
    const auto& source = state.source.get();
    const unsigned channels = image::is_color(source.shape.model) ? image::rgb_channels : 1;
    const auto bytes = source.shape.depth / image::byte_bits;
    const auto stride = image::components(source.shape.model) * bytes;
    const auto maximum = source.shape.depth == image::word_bits ? image::word_max : image::byte_max;
    auto const input = state.input.view().row(0);
    auto const alpha = state.alpha.view().row(0);
    for (std::uint32_t i = 0; i < part.count; ++i) {
        const auto p = image::source_coordinate(source.shape, source.metadata.orientation,
                                                {.x = part.first + i, .y = part.y});
        const auto pixel = source.pixels.view().row(p.y).subspan(std::size_t{p.x} * stride, stride);
        const double opacity =
            image::has_alpha(source.shape.model)
                ? static_cast<double>(image::read_sample(
                      pixel.subspan(std::size_t{channels} * bytes), source.shape.depth)) /
                      maximum
                : 1.0;
        if (opacity < 1 && state.parameters.alpha == image::AlphaPolicy::reject) {
            return core::failure(core::ErrorCode::input,
                                 "PNG contains non-opaque samples under --alpha reject");
        }
        if (opacity < 1 && use == image::RowUse::output) {
            ++state.report.flattened_pixels;
        }
        alpha.subspan(i, 1).front() = static_cast<float>(opacity);
        for (unsigned c = 0; c < channels; ++c) {
            input.subspan((std::size_t{i} * channels) + c, 1).front() =
                opacity == 0 ? 0.0F
                             : static_cast<float>(
                                   static_cast<double>(image::read_sample(
                                       pixel.subspan(std::size_t{c} * bytes), source.shape.depth)) /
                                   maximum);
        }
    }
    return {};
}
core::Result<void> interpret(ConversionState& state, std::uint32_t count) {
    const unsigned channels =
        image::is_color(state.source.get().shape.model) ? image::rgb_channels : 1;
    const auto input = state.input.view().row(0);
    const auto linear = state.linear.view().row(0);
    if (state.transform) {
        cmsDoTransform(state.transform.get(), input.data(), linear.data(), count);
        if (!state.context.good()) {
            return std::unexpected(state.context.error("PNG color transformation failed"));
        }
        return {};
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        for (unsigned c = 0; c < image::rgb_channels; ++c) {
            const double sample = static_cast<double>(
                input.subspan((std::size_t{i} * channels) + (channels == 1 ? 0 : c), 1).front());
            const auto value = state.power_exponent
                                   ? core::Result<double>{std::pow(sample, *state.power_exponent)}
                                   : image::srgb_decode(sample);
            if (!value) {
                return std::unexpected(value.error());
            }
            linear.subspan((std::size_t{i} * image::rgb_channels) + c, 1).front() =
                static_cast<float>(*value);
        }
    }
    return {};
}
core::Result<std::array<double, image::rgb_channels>>
opaque_pixel(ConversionState& state, std::uint32_t pixel, image::RowUse use) {
    const auto in = state.linear.view().row(0).subspan(std::size_t{pixel} * image::rgb_channels,
                                                       image::rgb_channels);
    const double alpha = static_cast<double>(state.alpha.view().row(0).subspan(pixel, 1).front());
    const double matte = state.parameters.alpha == image::AlphaPolicy::black ? 0.0 : 1.0;
    std::array<double, image::rgb_channels> values{};
    for (unsigned c = 0; c < image::rgb_channels; ++c) {
        const double value = static_cast<double>(in.subspan(c, 1).front());
        if (!std::isfinite(value)) {
            return core::failure(core::ErrorCode::input,
                                 "Color transform produced a non-finite sample");
        }
        if ((value < 0 || value > 1) && use == image::RowUse::output) {
            ++state.report.clipped_components;
        }
        values.at(c) = (alpha * std::clamp(value, 0.0, 1.0)) + ((1.0 - alpha) * matte);
    }
    return values;
}
core::Result<void> quantize(ConversionState& state, RowPart part, std::span<std::uint8_t> output,
                            image::RowUse use) {
    const auto shape = state.report.output;
    const auto channels = image::components(shape.model);
    const auto bytes = shape.depth / image::byte_bits;
    for (std::uint32_t i = 0; i < part.count; ++i) {
        auto values = opaque_pixel(state, i, use);
        if (!values) {
            return std::unexpected(values.error());
        }
        const auto at = std::size_t{part.first + i} * channels * bytes;
        const auto result = image::quantize_linear(
            shape, *values, output.subspan(at, std::size_t{channels} * bytes));
        if (!result) {
            return result;
        }
    }
    return {};
}
} // namespace
core::Result<void> read_linear(ConversionState& state, image::RowRange range, std::span<double> rgb,
                               image::RowUse use) {
    const auto width = state.report.output.width;
    if (range.row >= state.report.output.height || range.first >= width || rgb.empty() ||
        rgb.size() % image::rgb_channels != 0 ||
        rgb.size() / image::rgb_channels > conversion_pixels ||
        rgb.size() / image::rgb_channels > width - range.first) {
        return core::failure(core::ErrorCode::argument, "Invalid interpreted linear block");
    }
    if (state.cancellation.requested(core::Checkpoint::processing)) {
        return core::cancelled();
    }
    const auto count = static_cast<std::uint32_t>(rgb.size() / image::rgb_channels);
    const RowPart part{.y = range.row, .first = range.first, .count = count};
    auto gathered = gather(state, part, use);
    if (!gathered) {
        return gathered;
    }
    auto interpreted = interpret(state, count);
    if (!interpreted) {
        return interpreted;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto value = opaque_pixel(state, i, use);
        if (!value) {
            return std::unexpected(value.error());
        }
        std::ranges::copy(*value, rgb.subspan(std::size_t{i} * image::rgb_channels).begin());
    }
    return {};
}
core::Result<void> convert_row(ConversionState& state, std::uint32_t row,
                               std::span<std::uint8_t> output, image::RowUse use) {
    const auto required = image::raster_row_bytes(state.report.output);
    if (!required || output.size() != *required || row >= state.report.output.height) {
        return core::failure(core::ErrorCode::argument,
                             "Output row extent does not match its descriptor");
    }
    for (std::uint32_t x = 0; x < state.report.output.width;) {
        if (state.cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const RowPart part{
            .y = row,
            .first = x,
            .count = std::min(conversion_pixels, state.report.output.width - x),
        };
        auto gathered = gather(state, part, use);
        if (!gathered) {
            return gathered;
        }
        auto interpreted = interpret(state, part.count);
        if (!interpreted) {
            return interpreted;
        }
        auto encoded = quantize(state, part, output, use);
        if (!encoded) {
            return encoded;
        }
        x += part.count;
    }
    return {};
}
} // namespace docenhance::color
