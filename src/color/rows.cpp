// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "context.hpp"
#include "conversion.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <lcms2.h>
#include <span>
#include <utility>
#include <variant>

namespace docenhance::color {
namespace {
struct RowPart {
    std::uint32_t y{};
    std::uint32_t first{};
    std::uint32_t count{};
};
unsigned channels(const InterpretationState& state) noexcept {
    return image::interpretation_category(state.source.get().shape.model) ==
                   image::InterpretationCategory::gray1
               ? 1
               : image::rgb_channels;
}
core::Result<double> straight_sample(std::uint16_t stored, std::uint32_t maximum, double opacity,
                                     const image::TiffDeclarations* const tiff) {
    if (opacity == 0) {
        return 0.0;
    }
    double value = static_cast<double>(stored) / maximum;
    if (tiff != nullptr && tiff->associated_alpha) {
        if (value > opacity) {
            return core::failure(core::ErrorCode::input, "Associated TIFF color exceeds alpha");
        }
        value /= opacity;
    }
    if (tiff != nullptr && tiff->miniswhite) {
        value = 1 - value;
    }
    return value;
}
core::Result<image::BlockObservation> gather(InterpretationReaderState& state, RowPart part) {
    const auto& source = state.run.get().source.get();
    const auto colors = channels(state.run.get());
    const auto bytes = source.shape.depth.bytes();
    const auto stride = image::components(source.shape.model) * bytes;
    const auto maximum =
        source.shape.depth == image::SampleDepth::word() ? image::word_max : image::byte_max;
    const auto* const tiff = std::get_if<image::TiffDeclarations>(&source.metadata.declarations);
    const auto input = state.input.view().row(0);
    const auto alpha = state.alpha.view().row(0);
    const auto oriented = image::oriented_shape(source.shape, source.metadata.orientation);
    image::BlockObservation observation;
    for (std::uint32_t i = 0; i < part.count; ++i) {
        const auto b = image::rotation_source_coordinate(oriented, state.run.get().report.rotation,
                                                         {.x = part.first + i, .y = part.y});
        const auto p = image::source_coordinate(source.shape, source.metadata.orientation, b);
        const auto pixel = source.pixels.view().row(p.y).subspan(std::size_t{p.x} * stride, stride);
        const double opacity =
            image::has_alpha(source.shape.model)
                ? static_cast<double>(image::read_sample(pixel.subspan(std::size_t{colors} * bytes),
                                                         source.shape.depth)) /
                      maximum
                : 1.0;
        if (opacity < 1 && state.run.get().parameters.alpha == image::AlphaPolicy::reject) {
            return core::failure(core::ErrorCode::input,
                                 "Source contains non-opaque samples under --alpha reject");
        }
        observation.flattened_pixels += static_cast<std::uint64_t>(opacity < 1);
        alpha.subspan(i, 1).front() = opacity;
        for (unsigned c = 0; c < colors; ++c) {
            auto value = straight_sample(
                image::read_sample(pixel.subspan(std::size_t{c} * bytes), source.shape.depth),
                maximum, opacity, tiff);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            input.subspan((std::size_t{i} * colors) + c, 1).front() = *value;
        }
    }
    return observation;
}
core::Result<void> interpret(InterpretationReaderState& state, std::uint32_t count) {
    const auto colors = channels(state.run.get());
    const auto input = state.input.view().row(0);
    const auto linear = state.linear.view().row(0);
    if (state.transform) {
        // The public DBL boundary uses the pinned LCMS float32 native evaluator internally.
        // Gray has a genuine GrayData/D50/gamma1 destination and one output sample per pixel.
        cmsDoTransform(state.transform.get(), input.data(), linear.data(), count);
        if (!state.run.get().context.good()) {
            return std::unexpected(
                state.run.get().context.error("Source color transformation failed"));
        }
        return {};
    }
    for (std::size_t i = 0; i < std::size_t{count} * colors; ++i) {
        const double sample = input.subspan(i, 1).front();
        linear.subspan(i, 1).front() = state.run.get().power_exponent
                                           ? std::pow(sample, *state.run.get().power_exponent)
                                           : image::srgb_decode(sample);
    }
    return {};
}
core::Result<void> composite(InterpretationReaderState& state, std::span<double> output,
                             image::BlockObservation& observation) {
    const auto colors = channels(state.run.get());
    const auto linear = state.linear.view().row(0);
    const auto alpha = state.alpha.view().row(0);
    const double matte = state.run.get().parameters.alpha == image::AlphaPolicy::black ? 0.0 : 1.0;
    for (std::size_t i = 0; i < output.size(); ++i) {
        const auto value = linear.subspan(i, 1).front();
        if (!std::isfinite(value)) {
            return core::failure(core::ErrorCode::input,
                                 "Color transform produced a non-finite sample");
        }
        observation.clipped_components += static_cast<std::uint64_t>(value < 0 || value > 1);
        const auto opacity = alpha.subspan(i / colors, 1).front();
        output.subspan(i, 1).front() =
            (opacity * std::clamp(value, 0.0, 1.0)) + ((1.0 - opacity) * matte);
    }
    return {};
}
} // namespace
core::Result<image::BlockObservation> read_interpretation(InterpretationReaderState& state,
                                                          std::uint32_t row, std::uint32_t first,
                                                          std::span<double> output) {
    const auto colors = channels(state.run.get());
    const auto shape = state.run.get().report.output;
    if (row >= shape.height || first >= shape.width || output.empty() ||
        output.size() % colors != 0 || output.size() / colors > conversion_pixels ||
        output.size() / colors > shape.width - first) {
        return core::failure(core::ErrorCode::argument, "Invalid interpreted linear block");
    }
    if (image::overlaps(output, state.linear.view().row(0)) &&
        output.data() != state.linear.view().row(0).data()) {
        return core::failure(core::ErrorCode::argument,
                             "Interpreted output may only alias its scratch at the first sample");
    }
    if (state.run.get().cancellation.requested(core::Checkpoint::processing)) {
        return core::cancelled();
    }
    const auto count = static_cast<std::uint32_t>(output.size() / colors);
    auto observation = gather(state, {.y = row, .first = first, .count = count});
    if (!observation) {
        return std::unexpected(std::move(observation.error()));
    }
    if (auto result = interpret(state, count); !result) {
        return std::unexpected(std::move(result.error()));
    }
    if (auto result = composite(state, output, *observation); !result) {
        return std::unexpected(std::move(result.error()));
    }
    return *observation;
}
} // namespace docenhance::color
