// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/morphology_facts.hpp"

#include "docenhance/core/range.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"
#include "docenhance/methods/parameter_domains.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "illumination_fact_checks.hpp"
#include "illumination_gain.hpp"
#include "morphology_detail.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace docenhance::methods {
bool validate(const MorphologyAnalysis& analysis) noexcept {
    constexpr auto maximum_stride =
        (image::source_pixels_max + morphology_lattice_extent - 1) / morphology_lattice_extent;
    const bool domains = analysis.stride != 0 && analysis.stride <= maximum_stride &&
                         analysis.count != 0 && analysis.count <= illumination_sample_limit &&
                         (!analysis.fallback || (analysis.count == 1 && analysis.stride != 1)) &&
                         core::in_range(analysis.analysis_fill, 0.0, 1.0) &&
                         core::in_range(analysis.target, 0.0, 1.0) &&
                         core::in_range(analysis.background_min, 0.0, 1.0) &&
                         core::in_range(analysis.background_q10, analysis.background_min, 1.0) &&
                         core::in_range(analysis.background_q50, analysis.background_q10, 1.0) &&
                         core::in_range(analysis.background_q90, analysis.background_q50, 1.0) &&
                         core::in_range(analysis.background_max, analysis.background_q90, 1.0);
    if (!domains) {
        return false;
    }
    const auto low = image::nearest_rank_index(analysis.count, 0.1);
    const auto middle = image::nearest_rank_index(analysis.count, 0.5);
    const auto high = image::nearest_rank_index(analysis.count, 0.9);
    return low && middle && high &&
           (*low != *middle || analysis.background_q10 == analysis.background_q50) &&
           (*middle != *high || analysis.background_q50 == analysis.background_q90);
}
namespace {
bool untouched(const MorphologyFacts& facts) noexcept {
    return !facts.analysis && facts.counts.evaluated == 0 && facts.counts.changed == 0 &&
           facts.gain_capped_samples == 0 && facts.saturated_samples == 0 && facts.min_gain == 1 &&
           facts.max_gain == 1;
}
IlluminationApplicationFacts application_facts(const MorphologyFacts& facts) noexcept {
    return {
        .counts = facts.counts,
        .gain_capped_samples = facts.gain_capped_samples,
        .saturated_samples = facts.saturated_samples,
        .min_gain = facts.min_gain,
        .max_gain = facts.max_gain,
    };
}
bool full_measurements_agree(const MorphologyAnalysis& analysis, std::uint64_t total) noexcept {
    if (analysis.count == total) {
        const auto low = image::nearest_rank_index(analysis.count, 0.1);
        const auto high = image::nearest_rank_index(analysis.count, 0.9);
        if (!low || !high || (*low == 0 && analysis.background_q10 != analysis.background_min) ||
            (*high + 1 == total && analysis.background_q90 != analysis.background_max)) {
            return false;
        }
    }
    return total != 1 || (analysis.analysis_fill == analysis.background_min &&
                          analysis.background_min == analysis.background_max);
}
bool active(const MorphologyFacts& facts) noexcept {
    const auto total = facts.counts.eligible + facts.counts.protected_samples;
    if (!facts.analysis || !validate(*facts.analysis) ||
        facts.counts.eligible < std::min(morphology_minimum_samples, total) ||
        facts.counts.evaluated != facts.counts.eligible ||
        facts.analysis->count > facts.counts.eligible ||
        (facts.counts.evaluated == 1 && facts.min_gain != facts.max_gain)) {
        return false;
    }
    const auto application = application_facts(facts);
    if (!illumination_source_counts(application, facts.analysis->count,
                                    facts.analysis->analysis_fill) ||
        !illumination_effect_counts(application)) {
        return false;
    }
    if (facts.analysis->target <= std::max(facts.analysis->background_min, illumination_floor) &&
        facts.max_gain != 1) {
        return false;
    }
    if (facts.analysis->target <= std::max(facts.analysis->background_q90, illumination_floor) &&
        facts.min_gain != 1) {
        return false;
    }
    return full_measurements_agree(*facts.analysis, total);
}

bool sampling_agrees(const MorphologyFacts& facts, image::Extent extent) noexcept {
    if (!facts.analysis) {
        return false;
    }
    const auto& analysis = *facts.analysis;
    const auto stride = morphology_lattice_stride(extent);
    const auto columns = std::uint64_t{1} + ((extent.width - 1) / stride);
    const auto rows = std::uint64_t{1} + ((extent.height - 1) / stride);
    const auto lattice = columns * rows;
    if (analysis.stride != stride || analysis.count > lattice) {
        return false;
    }
    if (analysis.fallback) {
        return stride != 1 && facts.counts.protected_samples >= lattice;
    }
    return lattice <= facts.counts.protected_samples ||
           analysis.count >= lattice - facts.counts.protected_samples;
}
} // namespace
bool validate(const MorphologyFacts& facts) noexcept {
    if (!validate(facts.counts) || !background_radius_domain.contains(facts.resolved_radius) ||
        facts.sigma != morphology_sigma(facts.resolved_radius) ||
        facts.gaussian_radius != morphology_gaussian_radius(facts.resolved_radius) ||
        !background_max_gain_domain.contains(facts.min_gain) ||
        !background_max_gain_domain.contains(facts.max_gain) || facts.min_gain > facts.max_gain ||
        facts.gain_capped_samples > facts.counts.evaluated ||
        facts.saturated_samples > facts.counts.evaluated) {
        return false;
    }
    switch (facts.reason) {
    case MorphologyReason::zero_strength:
    case MorphologyReason::unit_gain:
        return facts.disposition == Disposition::no_change && untouched(facts);
    case MorphologyReason::no_eligible_samples:
        return facts.disposition == Disposition::no_change && facts.counts.eligible == 0 &&
               untouched(facts);
    case MorphologyReason::none:
        return facts.disposition == Disposition::applied && facts.counts.changed != 0 &&
               active(facts);
    case MorphologyReason::no_effect:
        return facts.disposition == Disposition::no_change && facts.counts.changed == 0 &&
               active(facts);
    }
    return false;
}
bool validate(const MorphologyFacts& facts, MorphologyParameters parameters,
              image::Extent extent) noexcept {
    if (!validate(facts) || !valid_parameters(parameters) || extent.width == 0 ||
        extent.height == 0 ||
        !counts_match(facts.counts, std::uint64_t{extent.width} * extent.height)) {
        return false;
    }
    const auto method = Morphology::create(parameters);
    if (!method || facts.resolved_radius != morphology_radius(extent, *method)) {
        return false;
    }
    if (parameters.strength == 0) {
        return facts.reason == MorphologyReason::zero_strength;
    }
    if (parameters.max_gain == 1) {
        return facts.reason == MorphologyReason::unit_gain;
    }
    if (facts.counts.eligible == 0) {
        return facts.reason == MorphologyReason::no_eligible_samples;
    }
    if (!facts.analysis || !sampling_agrees(facts, extent) ||
        facts.max_gain > parameters.max_gain ||
        ((facts.max_gain == parameters.max_gain) != (facts.gain_capped_samples != 0)) ||
        ((facts.min_gain == parameters.max_gain) !=
         (facts.gain_capped_samples == facts.counts.evaluated)) ||
        facts.analysis->target != parameters.target.value_or(facts.analysis->background_q90)) {
        return false;
    }
    const auto lower_gain =
        std::clamp(illumination_gain_ratio(facts.analysis->background_max, facts.analysis->target),
                   1.0, parameters.max_gain);
    const auto upper_gain =
        std::clamp(illumination_gain_ratio(facts.analysis->background_min, facts.analysis->target),
                   1.0, parameters.max_gain);
    if (facts.min_gain < lower_gain || facts.max_gain > upper_gain) {
        return false;
    }
    if (facts.counts.protected_samples == 0 &&
        (facts.min_gain != lower_gain || facts.max_gain != upper_gain)) {
        return false;
    }
    return (facts.reason == MorphologyReason::none ||
            facts.reason == MorphologyReason::no_effect) &&
           illumination_quantile_gains(application_facts(facts), facts.analysis->count,
                                       std::array{
                                           facts.analysis->background_q10,
                                           facts.analysis->background_q50,
                                           facts.analysis->background_q90,
                                       },
                                       facts.analysis->target, parameters.max_gain);
}
} // namespace docenhance::methods
