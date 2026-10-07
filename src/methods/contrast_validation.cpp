// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "contrast_detail.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/contrast.hpp"

#include <cmath>
#include <cstdint>
#include <type_traits>
#include <variant>
namespace docenhance::methods {
namespace {
bool empty_application(const ContrastReport& r) noexcept {
    return r.evaluated_samples == 0 && r.corrected_samples == 0 && r.changed_samples == 0 &&
           r.clipped_low_samples == 0 && r.clipped_high_samples == 0;
}
bool parameters(const ContrastParameters& p) {
    return std::visit(
        [](const auto& v) {
            using P = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<P, LevelsParameters>) {
                return Levels::create(v).has_value();
            } else {
                if constexpr (std::is_same_v<P, GammaParameters>) {
                    return Gamma::create(v).has_value();
                } else {
                    return Clahe::create(v).has_value();
                }
            }
        },
        p);
}
ContrastReason noop_reason(const ContrastReport& r) {
    if (!r.requested) {
        return ContrastReason::none;
    }
    const double blend = std::visit([](const auto& p) { return p.blend; }, *r.requested);
    if (blend == 0) {
        return ContrastReason::zero_blend;
    }
    if (r.eligible_samples == 0) {
        return ContrastReason::no_eligible_samples;
    }
    if (const auto* const p = std::get_if<GammaParameters>(&*r.requested);
        p != nullptr && p->gamma == 1) {
        return ContrastReason::identity_gamma;
    }
    if (std::holds_alternative<LevelsParameters>(*r.requested) && r.levels &&
        r.levels->high - r.levels->low < levels_minimum_range) {
        return ContrastReason::insufficient_dynamic_range;
    }
    return ContrastReason::none;
}
bool valid_measurement(const ContrastReport& r) {
    if (!r.requested) {
        return false;
    }
    if (std::holds_alternative<LevelsParameters>(*r.requested)) {
        return r.levels.has_value() && r.measured_samples == r.eligible_samples;
    }
    if (const auto* const clahe = std::get_if<ClaheParameters>(&*r.requested)) {
        auto shape = image::plane_shape(clahe_bins + 1, clahe->grid_columns * clahe->grid_rows,
                                        sizeof(double));
        const auto statistics = image::plane_shape(
            clahe_statistic_channels, clahe->grid_columns * clahe->grid_rows, sizeof(double));
        if (!shape || !statistics || r.measured_samples != r.eligible_samples ||
            r.preparation_charge_peak < image::plane_bytes(*shape).value() +
                                            image::plane_bytes(*statistics).value() +
                                            (std::uint64_t{image::linear_block_pixels} *
                                             image::rgb_channels * sizeof(double))) {
            return false;
        }
    }
    return true;
}
} // namespace
bool valid_contrast_observations(const ContrastReport& r) {
    if (static_cast<unsigned>(r.status) > static_cast<unsigned>(ContrastStatus::failed) ||
        static_cast<unsigned>(r.reason) >
            static_cast<unsigned>(ContrastReason::processing_failure) ||
        r.eligible_samples > image::source_pixels_max ||
        r.protected_samples > image::source_pixels_max - r.eligible_samples ||
        r.measured_samples > r.eligible_samples || r.evaluated_samples > r.eligible_samples ||
        r.corrected_samples > r.evaluated_samples || r.changed_samples > r.corrected_samples ||
        r.clipped_low_samples > r.evaluated_samples ||
        r.clipped_high_samples > r.evaluated_samples - r.clipped_low_samples ||
        r.preparation_charge_peak > core::continuous_processing_budget ||
        (r.complete && r.status == ContrastStatus::failed)) {
        return false;
    }
    if (!r.requested) {
        return r.status == ContrastStatus::disabled && r.reason == ContrastReason::none &&
               !r.levels && r.identity_tiles == 0 && r.measured_samples == 0 &&
               r.preparation_charge_peak == 0 && empty_application(r);
    }
    if (!parameters(*r.requested) || r.status == ContrastStatus::disabled) {
        return false;
    }
    const auto* const clahe = std::get_if<ClaheParameters>(&*r.requested);
    if (clahe != nullptr) {
        return !r.levels && r.identity_tiles <= clahe->grid_columns * clahe->grid_rows &&
               r.clipped_low_samples == 0 && r.clipped_high_samples == 0 &&
               (r.evaluated_samples == 0 || r.measured_samples == r.eligible_samples);
    }
    if (r.identity_tiles != 0) {
        return false;
    }
    if (std::holds_alternative<GammaParameters>(*r.requested)) {
        return !r.levels && r.measured_samples == 0 && r.clipped_low_samples == 0 &&
               r.clipped_high_samples == 0;
    }
    if (!r.levels) {
        return empty_application(r);
    }
    const auto v = *r.levels;
    if (!std::isfinite(v.low) || !std::isfinite(v.high) || v.low < 0 || v.high > 1 ||
        v.high < v.low || r.measured_samples == 0 || r.measured_samples != r.eligible_samples) {
        return false;
    }
    constexpr auto transfer_bytes =
        std::uint64_t{image::linear_block_pixels} * image::rgb_channels * sizeof(double);
    if (r.preparation_charge_peak < (r.measured_samples * sizeof(double)) + transfer_bytes) {
        return false;
    }
    return v.high - v.low >= levels_minimum_range || empty_application(r);
}
bool valid_contrast(const ContrastReport& r, const Contrast& method) {
    if (!r.complete || !valid_contrast_observations(r)) {
        return false;
    }
    const bool matches = std::visit(
        [&](const auto& selected) {
            using M = std::decay_t<decltype(selected)>;
            if constexpr (std::is_same_v<M, ContrastOff>) {
                return !r.requested;
            } else {
                return r.requested && *r.requested == ContrastParameters{selected.parameters()};
            }
        },
        method);
    if (!matches) {
        return false;
    }
    if (!r.requested) {
        return true;
    }
    const auto reason = noop_reason(r);
    if (reason != ContrastReason::none) {
        const bool measured = reason == ContrastReason::insufficient_dynamic_range;
        return r.status == ContrastStatus::no_change && r.reason == reason &&
               empty_application(r) &&
               (measured ? (r.levels.has_value() && r.measured_samples == r.eligible_samples)
                         : (!r.levels && r.identity_tiles == 0 && r.measured_samples == 0 &&
                            r.preparation_charge_peak == 0));
    }
    if (r.evaluated_samples != r.eligible_samples) {
        return false;
    }
    if (!valid_measurement(r)) {
        return false;
    }
    constexpr auto transfer_bytes =
        std::uint64_t{image::linear_block_pixels} * image::rgb_channels * sizeof(double);
    if (r.preparation_charge_peak < transfer_bytes) {
        return false;
    }
    return (r.status == ContrastStatus::applied && r.reason == ContrastReason::none &&
            r.changed_samples > 0) ||
           (r.status == ContrastStatus::no_change && r.reason == ContrastReason::no_effect &&
            r.changed_samples == 0);
}
} // namespace docenhance::methods
