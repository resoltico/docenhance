// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/restoration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
namespace docenhance::methods {
namespace {
bool empty_application(const RestorationReport& r) {
    return r.evaluated_samples == 0 && r.corrected_samples == 0 && r.changed_samples == 0 &&
           r.raw_low_samples == 0 && r.raw_high_samples == 0 && r.blended_low_samples == 0 &&
           r.blended_high_samples == 0;
}
bool valid_requested_state(const RestorationReport& r) {
    if ((r.status != RestorationStatus::failed && r.status != RestorationStatus::no_change &&
         r.status != RestorationStatus::applied) ||
        (!r.complete && (r.status != RestorationStatus::failed ||
                         r.reason != RestorationReason::processing_failure)) ||
        (r.complete && r.status == RestorationStatus::applied &&
         r.reason != RestorationReason::none) ||
        (r.complete && r.status == RestorationStatus::no_change &&
         r.reason != RestorationReason::zero_blend &&
         r.reason != RestorationReason::no_eligible_samples &&
         r.reason != RestorationReason::no_effect)) {
        return false;
    }
    if ((r.fft_width == 0) != (r.fft_height == 0) ||
        (r.fft_width == 0 &&
         (r.guard != 0 || r.native_reserved_bytes != 0 || r.native_calls != 0 || r.padded_mean)) ||
        (r.native_calls != 0 && (!r.padded_mean || r.native_reserved_bytes == 0)) ||
        (r.padded_mean && r.context_samples != r.eligible_samples + r.protected_samples)) {
        return false;
    }
    return true;
}
} // namespace
bool canonical_psf(const WienerParameters& p, const ResolvedPsf& psf) {
    if (!validate_psf(psf)) {
        return false;
    }
    if (const auto* const file = std::get_if<FilePsf>(&p.psf)) {
        return psf.source_identity && psf.source_identity->path == file->path &&
               core::valid_path(psf.source_identity->path) &&
               psf.source_identity->identity.bytes > 0 &&
               psf.source_identity->identity.bytes <= core::psf_encoded_bytes_max &&
               core::valid_hexadecimal(psf.source_identity->identity.sha256,
                                       core::sha256_hex_length);
    }
    const auto side = psf_side(p.psf);
    if (!side || *side != psf.width || *side != psf.height || psf.source_identity) {
        return false;
    }
    constexpr std::size_t maximum_analytic_coefficients = std::size_t{35} * 35;
    std::array<double, maximum_analytic_coefficients> storage{};
    auto const expected = std::span<double>{storage}.first(std::size_t{*side} * (*side));
    const auto generated = std::holds_alternative<GaussianPsf>(p.psf)
                               ? gaussian_psf(std::get<GaussianPsf>(p.psf), expected)
                               : motion_psf(std::get<MotionPsf>(p.psf), expected);
    if (!generated) {
        return false;
    }
    constexpr double coefficient_tolerance = 1e-14;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (std::abs(expected.subspan(i, 1).front() -
                     std::span<const double>{psf.coefficients}.subspan(i, 1).front()) >
            coefficient_tolerance) {
            return false;
        }
    }
    return true;
}
bool valid_restoration_observations(const RestorationReport& r) {
    if (r.eligible_samples > image::source_pixels_max ||
        r.protected_samples > image::source_pixels_max - r.eligible_samples ||
        r.context_samples > r.eligible_samples + r.protected_samples ||
        r.evaluated_samples > r.eligible_samples || r.corrected_samples > r.evaluated_samples ||
        r.changed_samples > r.corrected_samples || r.raw_low_samples > r.evaluated_samples ||
        r.raw_high_samples > r.evaluated_samples - r.raw_low_samples ||
        r.blended_low_samples > r.evaluated_samples ||
        r.blended_high_samples > r.evaluated_samples - r.blended_low_samples ||
        r.native_calls > restoration_native_calls ||
        r.native_reserved_bytes > core::continuous_processing_budget ||
        r.preparation_charge_peak > core::continuous_processing_budget ||
        (r.complete && r.status == RestorationStatus::failed)) {
        return false;
    }
    if (!r.requested) {
        return r.status == RestorationStatus::disabled && r.reason == RestorationReason::none &&
               !r.psf && r.guard == 0 && r.fft_width == 0 && r.fft_height == 0 && !r.padded_mean &&
               r.context_samples == 0 && r.preparation_charge_peak == 0 &&
               r.native_reserved_bytes == 0 && r.native_calls == 0 && !r.inference_warning &&
               !r.after_transform_warning && !r.off_center_warning && empty_application(r);
    }
    if (!valid_requested_state(r)) {
        return false;
    }
    if (!Wiener::create(*r.requested) || r.status == RestorationStatus::disabled ||
        !r.inference_warning || (r.psf && !canonical_psf(*r.requested, *r.psf))) {
        return false;
    }
    if (r.psf && r.off_center_warning !=
                     (std::hypot(r.psf->centroid_x, r.psf->centroid_y) > psf_off_center_limit)) {
        return false;
    }
    if (!r.psf && r.off_center_warning) {
        return false;
    }
    if (r.padded_mean &&
        (!std::isfinite(*r.padded_mean) || *r.padded_mean < 0 || *r.padded_mean > 1)) {
        return false;
    }
    return r.evaluated_samples == 0 ||
           (r.psf && r.native_calls == restoration_native_calls && r.padded_mean &&
            r.context_samples == r.eligible_samples + r.protected_samples);
}
bool valid_restoration_extent(const RestorationReport& r, image::Extent extent) {
    const auto pixels = std::uint64_t{extent.width} * extent.height;
    if (extent.width == 0 || extent.height == 0 || pixels > image::source_pixels_max ||
        r.protected_samples > pixels || r.eligible_samples != pixels - r.protected_samples) {
        return false;
    }
    if (r.fft_width == 0 && r.fft_height == 0) {
        return r.guard == 0;
    }
    if (!r.psf || r.guard != std::max(restoration_minimum_guard,
                                      restoration_guard_radius_factor *
                                          std::max(r.psf->width / 2, r.psf->height / 2))) {
        return false;
    }
    const auto width =
        restoration_fft_size(std::uint64_t{extent.width} + (std::uint64_t{2} * r.guard));
    const auto height =
        restoration_fft_size(std::uint64_t{extent.height} + (std::uint64_t{2} * r.guard));
    if (!width || !height || r.fft_width != *width || r.fft_height != *height) {
        return false;
    }
    const auto charge = restoration_charge(extent, {.width = *width, .height = *height});
    return charge && (r.native_reserved_bytes == 0 || r.native_reserved_bytes == charge->native) &&
           (r.context_samples == 0 || r.preparation_charge_peak >= charge->peak);
}
bool valid_restoration(const RestorationReport& r, const Restoration& method) {
    if (!r.complete || !valid_restoration_observations(r)) {
        return false;
    }
    const auto* const selected = std::get_if<Wiener>(&method);
    if (selected == nullptr) {
        return !r.requested;
    }
    if (!r.requested || *r.requested != selected->parameters() || !r.psf) {
        return false;
    }
    if (selected->parameters().blend == 0 || r.eligible_samples == 0) {
        return r.status == RestorationStatus::no_change &&
               r.reason == (selected->parameters().blend == 0
                                ? RestorationReason::zero_blend
                                : RestorationReason::no_eligible_samples) &&
               r.context_samples == 0 && r.guard == 0 && r.fft_width == 0 && r.fft_height == 0 &&
               !r.padded_mean && r.native_calls == 0 && r.native_reserved_bytes == 0 &&
               empty_application(r);
    }
    return r.evaluated_samples == r.eligible_samples &&
           r.native_calls == restoration_native_calls && r.padded_mean &&
           r.context_samples == r.eligible_samples + r.protected_samples &&
           r.preparation_charge_peak != 0 && r.native_reserved_bytes != 0 &&
           (r.changed_samples == 0
                ? r.status == RestorationStatus::no_change &&
                      r.reason == RestorationReason::no_effect
                : r.status == RestorationStatus::applied && r.reason == RestorationReason::none);
}
} // namespace docenhance::methods
