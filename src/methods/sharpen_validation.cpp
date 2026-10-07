// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/limits.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <cmath>
#include <cstdint>
#include <variant>
namespace docenhance::methods {
namespace {
bool empty_application(const SharpenReport& r) {
    return !r.pre_clamp && r.evaluated_samples == 0 && r.corrected_samples == 0 &&
           r.changed_samples == 0 && r.clipped_low_samples == 0 && r.clipped_high_samples == 0;
}
} // namespace
bool valid_sharpen_observations(const SharpenReport& r) {
    if (r.eligible_samples > image::source_pixels_max ||
        r.protected_samples > image::source_pixels_max - r.eligible_samples ||
        r.context_samples > r.eligible_samples + r.protected_samples ||
        r.evaluated_samples > r.eligible_samples || r.corrected_samples > r.evaluated_samples ||
        r.changed_samples > r.corrected_samples || r.clipped_low_samples > r.evaluated_samples ||
        r.clipped_high_samples > r.evaluated_samples - r.clipped_low_samples ||
        r.clipped_low_samples + r.clipped_high_samples > r.corrected_samples ||
        r.preparation_charge_peak > core::continuous_processing_budget ||
        (r.complete && r.status == SharpenStatus::failed)) {
        return false;
    }
    if (!r.requested) {
        return r.status == SharpenStatus::disabled && r.reason == SharpenReason::none &&
               r.context_samples == 0 && r.preparation_charge_peak == 0 && empty_application(r);
    }
    if (!Unsharp::create(*r.requested) || r.status == SharpenStatus::disabled) {
        return false;
    }
    if (r.pre_clamp) {
        const auto range = *r.pre_clamp;
        if (!std::isfinite(range.low) || !std::isfinite(range.high) ||
            range.low < -r.requested->amount || range.high > (1 + r.requested->amount) ||
            range.low > range.high || r.evaluated_samples == 0 ||
            (range.low < 0) != (r.clipped_low_samples != 0) ||
            (range.high > 1) != (r.clipped_high_samples != 0)) {
            return false;
        }
    } else if (!empty_application(r)) {
        return false;
    }
    return r.evaluated_samples == 0 ||
           r.context_samples == r.eligible_samples + r.protected_samples;
}
bool valid_sharpen_extent(const SharpenReport& r, image::Extent extent) {
    const auto pixels = std::uint64_t{extent.width} * extent.height;
    if (extent.width == 0 || extent.height == 0 || pixels > image::source_pixels_max ||
        r.protected_samples > pixels || r.eligible_samples != pixels - r.protected_samples) {
        return false;
    }
    if (r.context_samples == 0) {
        return true;
    }
    const auto shape = image::plane_shape(extent.width, extent.height, sizeof(double));
    if (!shape) {
        return false;
    }
    const auto bytes = image::plane_bytes(*shape);
    constexpr auto transfer =
        std::uint64_t{image::linear_block_pixels} * image::rgb_channels * sizeof(double);
    return bytes && r.preparation_charge_peak >= (2 * (*bytes)) + transfer;
}
bool valid_sharpen(const SharpenReport& r, const Sharpening& method) {
    if (!r.complete || !valid_sharpen_observations(r)) {
        return false;
    }
    const auto* const selected = std::get_if<Unsharp>(&method);
    if (selected == nullptr) {
        return !r.requested;
    }
    if (!r.requested || *r.requested != selected->parameters()) {
        return false;
    }
    if (selected->parameters().amount == 0 || r.eligible_samples == 0) {
        return r.status == SharpenStatus::no_change &&
               r.reason == (selected->parameters().amount == 0
                                ? SharpenReason::zero_amount
                                : SharpenReason::no_eligible_samples) &&
               r.context_samples == 0 && r.preparation_charge_peak == 0 && empty_application(r);
    }
    return r.evaluated_samples == r.eligible_samples && r.pre_clamp &&
           r.context_samples == r.eligible_samples + r.protected_samples &&
           r.preparation_charge_peak != 0 &&
           (r.changed_samples == 0
                ? r.status == SharpenStatus::no_change && r.reason == SharpenReason::no_effect
                : r.status == SharpenStatus::applied && r.reason == SharpenReason::none);
}
} // namespace docenhance::methods
