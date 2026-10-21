// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/output.hpp"

#include "docenhance/image/geometry.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"

#include <cstdint>
#include <string_view>
namespace docenhance::image {
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

InterpretationCategory interpretation_category(SampleModel model) noexcept {
    return is_color(model) ? InterpretationCategory::color3 : InterpretationCategory::gray1;
}
bool valid_interpretation_policy(InterpretationPolicy p) noexcept {
    return (p.alpha == AlphaPolicy::white || p.alpha == AlphaPolicy::black ||
            p.alpha == AlphaPolicy::reject) &&
           (p.profile == ProfilePolicy::embedded || p.profile == ProfilePolicy::srgb);
}
bool validate(PngShape shape) noexcept {
    return shape.width != 0 && shape.height != 0 &&
           std::uint64_t{shape.width} * shape.height <= source_pixels_max &&
           (shape.model == SampleModel::gray || shape.model == SampleModel::rgb) &&
           (shape.depth.bits() != 1 || shape.model == SampleModel::gray);
}
bool validate(const InterpretationReport& r) noexcept {
    if (r.decoded.width == 0 || r.decoded.height == 0 || components(r.decoded.model) == 0 ||
        !validate(r.output) || interpretation_name(r.interpretation) == "invalid" ||
        (r.resolution && (r.resolution->x == 0 || r.resolution->y == 0))) {
        return false;
    }
    const auto shape = rotated_shape(oriented_shape(r.decoded, r.orientation), r.rotation);
    const auto pixels = std::uint64_t{r.decoded.width} * r.decoded.height;
    const auto count = interpretation_category(r.decoded.model) == InterpretationCategory::gray1
                           ? std::uint64_t{1}
                           : std::uint64_t{3};
    return pixels <= source_pixels_max && r.output.width == shape.width &&
           r.output.height == shape.height && r.flattened_pixels <= pixels &&
           r.clipped_components <= pixels * count &&
           (has_alpha(r.decoded.model) || r.flattened_pixels == 0) &&
           (r.depth_reduced ==
            (r.decoded.depth == SampleDepth::word() && r.output.depth.bits() == byte_bits));
}
} // namespace docenhance::image
