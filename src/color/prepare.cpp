// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "context.hpp"
#include "conversion.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"

#include <cstdint>
#include <expected>
#include <lcms2.h>
#include <utility>

namespace docenhance::color {
namespace {
core::Result<void> native_transform(ConversionState& state) {
    const auto& source = state.source.get();
    auto input = source_profile(state.context, source);
    if (!input) {
        return std::unexpected(input.error());
    }
    state.input_profile = std::move(*input);
    state.linear_profile = linear_rgb_profile(state.context);
    if (!state.input_profile || !state.linear_profile) {
        return std::unexpected(state.context.error("Cannot construct source color profiles"));
    }
    const cmsUInt32Number format =
        image::is_color(source.shape.model) ? TYPE_RGB_DBL : TYPE_GRAY_DBL;
    state.transform.reset(cmsCreateTransformTHR(
        state.context.get(), state.input_profile.get(), format, state.linear_profile.get(),
        TYPE_RGB_DBL, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE));
    if (!state.transform || !state.context.good()) {
        return std::unexpected(state.context.error("Cannot interpret source color profile"));
    }
    return {};
}
core::Result<void> prepare_png_interpretation(ConversionState& state,
                                              const image::PngDeclarations& png) {
    const auto& source = state.source.get();
    const auto& metadata = source.metadata;
    if (!png.cicp && !png.srgb && metadata.icc.empty()) {
        state.report.assumed_transfer = !png.gamma;
        state.report.assumed_primaries = image::is_color(source.shape.model) && !png.chromaticities;
    }
    auto valid = validate_declarations(metadata);
    if (!valid) {
        return valid;
    }
    if (!metadata.icc.empty() || png.chromaticities) {
        const auto prepared = native_transform(state);
        if (!prepared) {
            return prepared;
        }
        state.report.interpretation = metadata.icc.empty() ? image::Interpretation::chromaticities
                                                           : image::Interpretation::icc;
    }
    if (png.cicp || png.srgb) {
        state.transform.reset();
        state.report.interpretation =
            png.cicp ? image::Interpretation::cicp : image::Interpretation::srgb;
    } else if (!state.transform && png.gamma) {
        constexpr double png_scale = 100000.0;
        state.power_exponent = png_scale / *png.gamma;
        state.report.interpretation = image::Interpretation::gamma;
    }
    return {};
}
core::Result<void> prepare_transform(ConversionState& state) {
    if (state.parameters.profile == image::ProfilePolicy::srgb) {
        state.report.interpretation = image::Interpretation::overridden_srgb;
        return {};
    }
    const auto& source = state.source.get();
    const auto& metadata = source.metadata;
    if (const auto* const png = metadata.png()) {
        return prepare_png_interpretation(state, *png);
    }
    if (!metadata.icc.empty()) {
        const auto prepared = native_transform(state);
        if (!prepared) {
            return prepared;
        }
        state.report.interpretation = image::Interpretation::icc;
    } else {
        state.report.assumed_transfer = true;
        state.report.assumed_primaries = image::is_color(source.shape.model);
    }
    return {};
}
core::Result<void> allocate_rows(ConversionState& state, core::Budget& budget) {
    const std::uint32_t channels =
        image::is_color(state.source.get().shape.model) ? image::rgb_channels : 1;
    const std::uint32_t colors = conversion_pixels * channels;
    auto input = image::Plane<double>::allocate(budget, colors, 1);
    if (!input) {
        return std::unexpected(input.error());
    }
    state.input = std::move(*input);
    auto linear =
        image::Plane<double>::allocate(budget, conversion_pixels * image::rgb_channels, 1);
    if (!linear) {
        return std::unexpected(linear.error());
    }
    state.linear = std::move(*linear);
    auto alpha = image::Plane<double>::allocate(budget, conversion_pixels, 1);
    if (!alpha) {
        return std::unexpected(alpha.error());
    }
    state.alpha = std::move(*alpha);
    return {};
}
} // namespace
core::Result<void> prepare_conversion(ConversionState& state, core::Budget& budget) {
    const auto& source = state.source.get();
    state.report.source = source.shape;
    state.report.output = image::oriented_shape(source.shape, source.metadata.orientation);
    const bool gray =
        state.parameters.mode == image::ToneMode::gray || !image::is_color(source.shape.model);
    state.report.output.model = gray ? image::SampleModel::gray : image::SampleModel::rgb;
    if (state.parameters.depth != image::OutputDepth::automatic) {
        state.report.output.depth = state.parameters.depth == image::OutputDepth::word
                                        ? image::SampleDepth::word()
                                        : image::SampleDepth::byte();
    }
    state.report.depth_reduced = state.report.output.depth < source.shape.depth;
    state.report.orientation = source.metadata.orientation;
    state.report.resolution = source.metadata.resolution;
    if (state.report.resolution && source.metadata.orientation.transposed()) {
        std::swap(state.report.resolution->x, state.report.resolution->y);
    }
    auto interpretation = prepare_transform(state);
    if (!interpretation) {
        return interpretation;
    }
    if (state.cancellation.requested(core::Checkpoint::processing)) {
        return core::cancelled();
    }
    auto profile = output_profile(state.context, budget, gray, state.cancellation);
    if (!profile) {
        return std::unexpected(profile.error());
    }
    state.profile = std::move(*profile);
    return allocate_rows(state, budget);
}
} // namespace docenhance::color
