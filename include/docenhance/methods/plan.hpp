// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <variant>

namespace docenhance::methods {
struct Photometric {
    Illumination illumination;
    Denoising denoising;
    Restoration restoration;
    Contrast contrast;
    Sharpening sharpening;
};
struct ContinuousOutput {
    image::ToneMode mode = image::ToneMode::preserve;
    image::OutputDepth depth = image::OutputDepth::automatic;
};
struct BinaryOutput {
    Binarization method;
};
// Output representation is chosen after the same interpretation, geometry and fixed stages.
// Filesystem resources belong to ProcessRequest/Input, never to method configuration.
struct Plan {
    image::AlphaPolicy alpha = image::AlphaPolicy::white;
    image::ProfilePolicy profile = image::ProfilePolicy::embedded;
    image::QuarterTurn rotation;
    Photometric photometric;
    std::variant<ContinuousOutput, BinaryOutput> output;
    bool protection = false;
};
[[nodiscard]] inline bool validate(const Plan& plan) noexcept {
    const auto& stages = plan.photometric;
    if (!image::valid_interpretation_policy(
            {.alpha = plan.alpha, .profile = plan.profile, .rotation = plan.rotation}) ||
        stages.illumination.valueless_by_exception() || stages.denoising.valueless_by_exception() ||
        stages.restoration.valueless_by_exception() || stages.contrast.valueless_by_exception() ||
        stages.sharpening.valueless_by_exception() || plan.output.valueless_by_exception()) {
        return false;
    }
    if (const auto* const wiener = std::get_if<Wiener>(&stages.restoration);
        wiener != nullptr && wiener->parameters().psf.valueless_by_exception()) {
        return false;
    }
    if (const auto* const continuous = std::get_if<ContinuousOutput>(&plan.output)) {
        return (continuous->mode == image::ToneMode::preserve ||
                continuous->mode == image::ToneMode::gray) &&
               (continuous->depth == image::OutputDepth::automatic ||
                continuous->depth == image::OutputDepth::byte ||
                continuous->depth == image::OutputDepth::word);
    }
    const auto* const binary = std::get_if<BinaryOutput>(&plan.output);
    return binary != nullptr && !binary->method.valueless_by_exception();
}
} // namespace docenhance::methods
