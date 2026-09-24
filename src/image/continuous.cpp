// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/image/continuous.hpp"

#include "docenhance/core/result.hpp"

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
} // namespace docenhance::image
