// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/limits.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <variant>
namespace docenhance::methods {
namespace {
bool objective(const std::optional<double>& value) noexcept {
    return !value || (std::isfinite(*value) && *value >= 0);
}
bool stopping(const Tvl1Report& s, const Tvl1Parameters& p) noexcept {
    if (!s.stop) {
        return !s.objective_end;
    }
    constexpr auto earliest_stop =
        tvl1_first_checkpoint + ((tvl1_required_passes - 1) * tvl1_checkpoint_period);
    if (*s.stop == Tvl1Stop::tolerance_met) {
        return s.iterations >= earliest_stop && s.iterations % tvl1_checkpoint_period == 0 &&
               s.passing_checkpoints == tvl1_required_passes && s.primal_update <= p.tolerance &&
               s.dual_update <= p.tolerance;
    }
    return *s.stop == Tvl1Stop::iteration_limit && s.iterations == p.iterations &&
           s.passing_checkpoints < tvl1_required_passes;
}
bool checkpoint_consistent(const Tvl1Report& s, const Tvl1Parameters& p) noexcept {
    if (s.iterations < tvl1_first_checkpoint || s.iterations % tvl1_checkpoint_period != 0) {
        return true;
    }
    const bool passed = s.primal_update <= p.tolerance && s.dual_update <= p.tolerance;
    return passed == (s.passing_checkpoints != 0);
}
} // namespace
bool valid_tvl1_observations(const DenoisingReport& r) {
    if (!r.requested || !std::holds_alternative<Tvl1Parameters>(*r.requested) || r.native_h != 0 ||
        r.native_calls != 0 || r.native_reserved_peak != 0 || r.status == DenoiseStatus::disabled ||
        r.preparation_charge_peak > core::continuous_processing_budget) {
        return false;
    }
    const auto p = std::get<Tvl1Parameters>(*r.requested);
    if (!Tvl1::create(p)) {
        return false;
    }
    if (!r.tvl1) {
        return !r.complete;
    }
    const auto& s = *r.tvl1;
    constexpr std::uint64_t workspace_planes = 5;
    constexpr auto transfer_bytes =
        std::uint64_t{image::linear_block_pixels} * image::rgb_channels * sizeof(double);
    if ((!s.objective_start && (s.iterations != 0 || s.stop || s.passing_checkpoints != 0 ||
                                s.primal_update != 0 || s.dual_update != 0)) ||
        (s.iterations < tvl1_first_checkpoint && s.passing_checkpoints != 0) ||
        (s.passing_checkpoints == tvl1_required_passes && s.stop != Tvl1Stop::tolerance_met) ||
        (s.field_bytes > 0 &&
         r.preparation_charge_peak < (workspace_planes * s.field_bytes) + transfer_bytes)) {
        return false;
    }
    constexpr double dual_roundoff = 16 * std::numeric_limits<double>::epsilon();
    return s.iterations <= p.iterations && s.passing_checkpoints <= tvl1_required_passes &&
           std::isfinite(s.primal_update) && s.primal_update >= 0 && s.primal_update <= 1 &&
           std::isfinite(s.dual_update) && s.dual_update >= 0 &&
           s.dual_update <= 2 + dual_roundoff && objective(s.objective_start) &&
           objective(s.objective_end) && s.field_bytes <= core::continuous_processing_budget &&
           s.field_bytes <= r.preparation_charge_peak &&
           (!s.objective_start || s.field_bytes > 0) && (s.iterations == 0 || s.objective_start) &&
           stopping(s, p) && checkpoint_consistent(s, p);
}
bool valid_tvl1_report(const DenoisingReport& r, const Tvl1& method) {
    if (!r.complete || !r.requested || !r.tvl1 || !valid_tvl1_observations(r) ||
        *r.requested != DenoisingParameters{method.parameters()} ||
        r.status == DenoiseStatus::failed) {
        return false;
    }
    const auto& s = *r.tvl1;
    const auto p = method.parameters();
    if (p.blend == 0 || r.eligible_samples == 0) {
        const auto reason =
            p.blend == 0 ? DenoiseReason::zero_blend : DenoiseReason::no_eligible_samples;
        return r.status == DenoiseStatus::no_change && r.reason == reason && s == Tvl1Report{} &&
               r.evaluated_samples == 0 && r.corrected_samples == 0 && r.changed_samples == 0 &&
               r.preparation_charge_peak == 0;
    }
    if (!s.stop || !s.objective_start || !s.objective_end || s.field_bytes == 0 ||
        r.evaluated_samples != r.eligible_samples) {
        return false;
    }
    return (r.status == DenoiseStatus::applied && r.reason == DenoiseReason::none &&
            r.changed_samples > 0) ||
           (r.status == DenoiseStatus::no_change && r.reason == DenoiseReason::no_effect &&
            r.changed_samples == 0);
}
} // namespace docenhance::methods
