// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "illumination_fact_checks.hpp"

#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/report_values.hpp"
#include "illumination_gain.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace docenhance::methods {
bool illumination_effect_counts(const IlluminationApplicationFacts& facts) noexcept {
    if (facts.counts.evaluated == 0 ||
        (facts.max_gain == 1 && (facts.counts.changed != 0 || facts.saturated_samples != 0 ||
                                 facts.gain_capped_samples != 0))) {
        return false;
    }
    return facts.min_gain != 1 || (facts.counts.changed < facts.counts.evaluated &&
                                   facts.saturated_samples < facts.counts.evaluated);
}
bool illumination_source_counts(const IlluminationApplicationFacts& facts, std::uint32_t samples,
                                double source_q90) noexcept {
    if (!validate(facts.counts) || samples == 0 || samples > facts.counts.evaluated) {
        return false;
    }
    if (source_q90 != 0 && source_q90 != 1) {
        return true;
    }
    const auto high = image::nearest_rank_index(samples, 0.9);
    if (!high) {
        return false;
    }
    const auto rank = static_cast<std::uint64_t>(*high);
    const auto fixed = source_q90 == 0 ? rank + 1 : samples - rank;
    const auto remaining = facts.counts.evaluated - fixed;
    return facts.counts.changed <= remaining &&
           (source_q90 != 0 || facts.saturated_samples <= remaining);
}
bool illumination_quantile_gains(
    const IlluminationApplicationFacts& facts, std::uint32_t samples,
    const std::array<double, illumination_quantile_count>& background_quantiles, double target,
    double cap) noexcept {
    if (!validate(facts.counts) || samples == 0 || samples > facts.counts.evaluated) {
        return false;
    }
    const std::array quantiles{
        std::pair{0.1, std::get<0>(background_quantiles)},
        std::pair{0.5, std::get<1>(background_quantiles)},
        std::pair{0.9, std::get<2>(background_quantiles)},
    };
    // The sampled groups can overlap; each supplies its own independent bound.
    return std::ranges::all_of(
        quantiles, [&facts, samples, target, cap](const auto& point) noexcept {
            const auto& [fraction, background] = point;
            const auto selected = image::nearest_rank_index(samples, fraction);
            if (!selected) {
                return false;
            }
            const auto rank = static_cast<std::uint64_t>(*selected);
            const auto remaining = facts.counts.evaluated - (samples - rank);
            const auto ratio = illumination_gain_ratio(background, target);
            const auto gain = std::clamp(ratio, 1.0, cap);
            return gain >= facts.min_gain && gain <= facts.max_gain &&
                   (ratio < cap || facts.gain_capped_samples >= rank + 1) &&
                   (ratio >= cap || facts.gain_capped_samples <= remaining) &&
                   (ratio > 1 ||
                    (facts.counts.changed <= remaining && facts.saturated_samples <= remaining));
        });
}
} // namespace docenhance::methods
