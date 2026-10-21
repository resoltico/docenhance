// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "restoration_detail.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace docenhance::methods {
namespace {
bool canonical_scalars(const PsfFacts& facts, const Psf& parameters) noexcept {
    if (std::holds_alternative<Kernel>(parameters)) {
        return facts.kind == PsfKind::kernel;
    }
    const auto side = psf_side(parameters);
    if (!side || *side != facts.width || *side != facts.height) {
        return false;
    }
    const auto* const gaussian = std::get_if<GaussianPsf>(&parameters);
    const auto* const motion = std::get_if<MotionPsf>(&parameters);
    if ((gaussian != nullptr && facts.kind != PsfKind::gaussian) ||
        (gaussian == nullptr && (motion == nullptr || facts.kind != PsfKind::motion))) {
        return false;
    }
    std::array<double, restoration_analytic_coefficients_max> coefficients{};
    const auto values = std::span{coefficients}.first(std::size_t{*side} * (*side));
    if (gaussian == nullptr && motion == nullptr) {
        return false;
    }
    const auto generated =
        gaussian != nullptr ? gaussian_psf(*gaussian, values) : motion_psf(*motion, values);
    if (!generated) {
        return false;
    }
    const auto centroid = psf_centroid(*side, *side, values);
    if (!centroid) {
        return false;
    }
    double sum = 0;
    for (const auto value : values) {
        sum += value;
    }
    return std::abs(sum - facts.normalized_sum) <= psf_normalization_tolerance &&
           std::abs(centroid->front() - facts.centroid_x) <= psf_normalization_tolerance &&
           std::abs(centroid->back() - facts.centroid_y) <= psf_normalization_tolerance;
}
} // namespace
bool validate(const WienerFacts& facts, const WienerParameters& parameters,
              image::Extent extent) noexcept {
    if (!validate(facts) || !valid_parameters(parameters) || extent.width == 0 ||
        extent.height == 0 ||
        !counts_match(facts.counts, std::uint64_t{extent.width} * extent.height) ||
        !canonical_scalars(facts.psf, parameters.psf)) {
        return false;
    }
    if (parameters.blend == 0) {
        return facts.reason == WienerReason::zero_blend;
    }
    if (facts.counts.eligible == 0) {
        return facts.reason == WienerReason::no_eligible_samples;
    }
    if (!facts.transform) {
        return false;
    }
    const auto guard = restoration_guard(facts.psf.width, facts.psf.height);
    const auto width =
        restoration_fft_size(std::uint64_t{extent.width} + (std::uint64_t{2} * guard));
    const auto height =
        restoration_fft_size(std::uint64_t{extent.height} + (std::uint64_t{2} * guard));
    if (!width || !height || facts.transform->width != *width ||
        facts.transform->height != *height) {
        return false;
    }
    if (parameters.blend == 1 &&
        (facts.blended_low_samples != facts.raw_low_samples ||
         facts.blended_high_samples != facts.raw_high_samples ||
         facts.counts.changed <
             facts.corrected_samples - facts.raw_low_samples - facts.raw_high_samples)) {
        return false;
    }
    return facts.reason == WienerReason::none || facts.reason == WienerReason::no_effect;
}
} // namespace docenhance::methods
