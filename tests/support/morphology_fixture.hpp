// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"

namespace docenhance::tests {
inline constexpr image::Extent morphology_fact_extent{.width = 16, .height = 16};
inline constexpr image::Extent morphology_singleton_extent{.width = 1, .height = 1};
inline constexpr methods::MorphologyParameters morphology_fact_parameters{};
inline methods::MorphologyFacts completed_morphology_facts() {
    return {
        .disposition = methods::Disposition::applied,
        .reason = methods::MorphologyReason::none,
        .counts = {.eligible = 256, .evaluated = 256, .changed = 100},
        .resolved_radius = 8,
        .sigma = 4,
        .gaussian_radius = 12,
        .analysis =
            methods::MorphologyAnalysis{
                .stride = 1,
                .count = 256,
                .analysis_fill = 0.7,
                .target = 0.8,
                .background_q10 = 0.3,
                .background_q50 = 0.5,
                .background_q90 = 0.8,
                .background_min = 0.2,
                .background_max = 0.9,
            },
        .gain_capped_samples = 64,
        .saturated_samples = 3,
        .min_gain = 1,
        .max_gain = 2,
    };
}
inline methods::MorphologyFacts black_morphology_facts() {
    return {
        .reason = methods::MorphologyReason::no_effect,
        .counts = {.eligible = 16, .evaluated = 16},
        .resolved_radius = 8,
        .sigma = 4,
        .gaussian_radius = 12,
        .analysis = methods::MorphologyAnalysis{.stride = 1, .count = 16, .target = 1},
        .gain_capped_samples = 16,
        .min_gain = 2,
        .max_gain = 2,
    };
}
} // namespace docenhance::tests
