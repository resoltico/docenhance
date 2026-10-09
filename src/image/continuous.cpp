// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/continuous.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"

#include <cstdint>
#include <string_view>

namespace docenhance::image {
core::Result<Continuous> Continuous::create(ToneParameters values) {
    const bool mode = values.mode == ToneMode::preserve || values.mode == ToneMode::gray;
    const bool depth = values.depth == OutputDepth::automatic ||
                       values.depth == OutputDepth::byte || values.depth == OutputDepth::word;
    const bool alpha = values.alpha == AlphaPolicy::white || values.alpha == AlphaPolicy::black ||
                       values.alpha == AlphaPolicy::reject;
    const bool profile =
        values.profile == ProfilePolicy::embedded || values.profile == ProfilePolicy::srgb;
    if (!mode || !depth || !alpha || !profile) {
        return core::failure(core::ErrorCode::argument, "Invalid continuous-tone policy");
    }
    return Continuous{values};
}
std::string_view interpretation_name(Interpretation value) noexcept {
    switch (value) {
    case Interpretation::assumed_srgb:
        return "assumed_srgb";
    case Interpretation::overridden_srgb:
        return "overridden_srgb";
    case Interpretation::srgb:
        return "srgb";
    case Interpretation::cicp:
        return "cicp_srgb";
    case Interpretation::icc:
        return "icc";
    case Interpretation::gamma:
        return "png_gamma";
    case Interpretation::chromaticities:
        return "png_chromaticities";
    }
    return "invalid";
}
namespace {
bool valid_shape(RasterShape shape) noexcept {
    return shape.width != 0 && shape.height != 0 && components(shape.model) != 0 &&
           std::uint64_t{shape.width} * shape.height <= source_pixels_max;
}
bool known_interpretation(Interpretation interpretation) noexcept {
    switch (interpretation) {
    case Interpretation::assumed_srgb:
    case Interpretation::overridden_srgb:
    case Interpretation::srgb:
    case Interpretation::cicp:
    case Interpretation::icc:
    case Interpretation::gamma:
    case Interpretation::chromaticities:
        return true;
    }
    return false;
}
} // namespace
bool valid_conversion(const ConversionReport& report, const Continuous& operation) noexcept {
    const auto& c = report;
    if (!c.verified || !valid_shape(c.source) || !valid_shape(c.output) ||
        !known_interpretation(c.interpretation) || has_alpha(c.output.model) ||
        (c.resolution && (c.resolution->x == 0 || c.resolution->y == 0))) {
        return false;
    }
    const auto pixels = std::uint64_t{c.output.width} * c.output.height;
    const auto p = operation.parameters();
    if (c.flattened_pixels > pixels || c.clipped_components > pixels * components(c.output.model) ||
        c.depth_reduced != (c.source.depth > c.output.depth) ||
        ((!has_alpha(c.source.model) || p.alpha == AlphaPolicy::reject) &&
         c.flattened_pixels != 0) ||
        ((c.interpretation == Interpretation::overridden_srgb) !=
         (p.profile == ProfilePolicy::srgb))) {
        return false;
    }
    const auto oriented = rotated_shape(oriented_shape(c.source, c.orientation), c.rotation);
    auto depth = c.source.depth;
    if (p.depth == OutputDepth::byte) {
        depth = SampleDepth::byte();
    } else if (p.depth == OutputDepth::word) {
        depth = SampleDepth::word();
    }
    const auto model = p.mode == ToneMode::gray || !is_color(c.source.model) ? SampleModel::gray
                                                                             : SampleModel::rgb;
    return c.output.width == oriented.width && c.output.height == oriented.height &&
           c.output.depth == depth && c.output.model == model;
}
} // namespace docenhance::image
