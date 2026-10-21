// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "context.hpp"
#include "conversion.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"

#include <expected>
#include <lcms2.h>
#include <utility>

namespace docenhance::color {
namespace {
core::Result<void> native_profiles(InterpretationState& state) {
    const auto& source = state.source.get();
    auto input = source_profile(state.context, source);
    if (!input) {
        return std::unexpected(input.error());
    }
    state.input_profile = std::move(*input);
    const bool color = image::is_color(source.shape.model);
    state.linear_profile =
        color ? linear_rgb_profile(state.context) : linear_gray_profile(state.context);
    if (!state.input_profile || !state.linear_profile) {
        return std::unexpected(state.context.error("Cannot construct source color profiles"));
    }
    const cmsUInt32Number format =
        image::is_color(source.shape.model) ? TYPE_RGB_DBL : TYPE_GRAY_DBL;
    state.initial_transform.reset(cmsCreateTransformTHR(
        state.context.get(), state.input_profile.get(), format, state.linear_profile.get(), format,
        INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE));
    if (!state.initial_transform || !state.context.good()) {
        return std::unexpected(state.context.error("Cannot interpret source color profile"));
    }
    state.native = true;
    return {};
}
core::Result<void> prepare_png_interpretation(InterpretationState& state,
                                              const image::PngDeclarations& png) {
    const auto& source = state.source.get();
    const auto& metadata = source.metadata;
    if (!png.cicp && !png.srgb && metadata.icc.empty()) {
        state.report.assumed_transfer = !png.gamma;
        state.report.assumed_primaries = image::is_color(source.shape.model) && !png.chromaticities;
    }
    if (!metadata.icc.empty() || png.chromaticities) {
        const auto prepared = native_profiles(state);
        if (!prepared) {
            return prepared;
        }
        state.report.interpretation = metadata.icc.empty() ? image::Interpretation::chromaticities
                                                           : image::Interpretation::icc;
    }
    if (png.cicp || png.srgb) {
        state.initial_transform.reset();
        state.native = false;
        state.report.interpretation =
            png.cicp ? image::Interpretation::cicp : image::Interpretation::srgb;
    } else if (!state.native && png.gamma) {
        constexpr double png_scale = 100000.0;
        state.power_exponent = png_scale / *png.gamma;
        state.report.interpretation = image::Interpretation::gamma;
    }
    return {};
}
core::Result<void> prepare_transform(InterpretationState& state) {
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
        const auto prepared = native_profiles(state);
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
} // namespace
core::Result<void> prepare_interpretation(InterpretationState& state) {
    const auto& source = state.source.get();
    state.report.decoded = source.shape;
    state.report.rotation = state.parameters.rotation;
    state.report.depth_reduced = source.shape.depth == image::SampleDepth::word() &&
                                 state.report.output.depth.bits() == image::byte_bits;
    state.report.orientation = source.metadata.orientation;
    state.report.resolution = source.metadata.resolution;
    if (state.report.resolution &&
        (source.metadata.orientation.transposed() != state.report.rotation.swaps_axes())) {
        std::swap(state.report.resolution->x, state.report.resolution->y);
    }
    auto interpretation = prepare_transform(state);
    if (!interpretation) {
        return interpretation;
    }
    if (state.cancellation.requested(core::Checkpoint::processing)) {
        return core::cancelled();
    }
    return {};
}
} // namespace docenhance::color
