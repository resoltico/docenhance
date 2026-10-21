// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "morphology_fixture.hpp"
#include "morphology_gain_cases.hpp"
#include "require.hpp"

namespace docenhance::tests {
inline void morphology_completion_fact_cases() {
    auto facts = completed_morphology_facts();
    constexpr image::Extent wrong_extent{.width = 32, .height = 16};
    require(methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "I02 completion retains its actual resolved kernel and full lattice statistics");
    require(!methods::validate(facts, methods::MorphologyParameters{.target = 0.7},
                               morphology_fact_extent),
            "I02 target facts must agree with an explicit requested target");
    require(!methods::validate(facts, methods::MorphologyParameters{.radius = 7},
                               morphology_fact_extent),
            "I02 radius facts must agree with the actual method owner");
    require(!methods::validate(facts, morphology_fact_parameters, wrong_extent),
            "I02 completion counts must match the actual canvas");
    facts.gaussian_radius = 11;
    require(!methods::validate(facts), "I02 cannot describe a different Gaussian support");
    facts.gaussian_radius = 12;
    facts.sigma = 3;
    require(!methods::validate(facts), "I02 cannot describe a different Gaussian sigma");
    facts.sigma = 4;
    facts.gain_capped_samples = 0;
    require(!methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "an attained I02 gain cap must have an observed capped destination");
}
inline void morphology_bypass_fact_cases() {
    methods::MorphologyFacts facts{
        .reason = methods::MorphologyReason::zero_strength,
        .counts = {.eligible = 256},
        .resolved_radius = 8,
        .sigma = 4,
        .gaussian_radius = 12,
    };
    require(methods::validate(facts, methods::MorphologyParameters{.strength = 0},
                              morphology_fact_extent),
            "zero I02 strength retains resolved geometry without analysis work");
    require(methods::validate(facts, methods::MorphologyParameters{.strength = 0, .max_gain = 1},
                              morphology_fact_extent),
            "zero I02 strength precedes unit gain");
    facts.reason = methods::MorphologyReason::unit_gain;
    require(methods::validate(facts, methods::MorphologyParameters{.max_gain = 1},
                              morphology_fact_extent),
            "unit I02 gain carries no analysis or evaluation");
    facts.counts = {.protected_samples = 256};
    require(methods::validate(facts, methods::MorphologyParameters{.max_gain = 1},
                              morphology_fact_extent),
            "unit I02 gain precedes fully protected completion");
    facts.reason = methods::MorphologyReason::no_eligible_samples;
    require(methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "fully protected I02 completion performs no preparation");
    facts.analysis = methods::MorphologyAnalysis{};
    require(!methods::validate(facts), "an I02 bypass cannot claim completed analysis");
}
inline void morphology_sampling_fact_cases() {
    auto facts = completed_morphology_facts();
    if (!facts.analysis) {
        require(false, "the completed I02 fixture owns analysis facts");
        return;
    }
    facts.analysis->analysis_fill = 1;
    require(methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "I02 source Y90 is not bounded by the smoothed background maximum");
    facts.analysis->analysis_fill = 0.7;
    facts.analysis->count = 255;
    require(!methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "unprotected I02 unit-stride sampling includes every pixel");
    facts.counts = {
        .eligible = 24576,
        .protected_samples = 8192,
        .evaluated = 24576,
        .changed = 100,
    };
    facts.analysis->stride = 2;
    facts.analysis->count = 1;
    facts.analysis->fallback = true;
    facts.analysis->background_q10 = 0.8;
    facts.analysis->background_q50 = 0.8;
    const image::Extent extent{.width = 2048, .height = 16};
    require(methods::validate(facts, morphology_fact_parameters, extent),
            "I02 fallback samples one eligible point while full-field extrema may differ");
    facts.analysis->background_q10 = 0.7;
    require(!methods::validate(facts), "single-sample I02 quantiles all name the same sample");
    facts.analysis->background_q10 = 0.8;
    facts.counts = {
        .eligible = 24577,
        .protected_samples = 8191,
        .evaluated = 24577,
        .changed = 100,
    };
    require(!methods::validate(facts, morphology_fact_parameters, extent),
            "I02 fallback requires enough protection to cover the entire lattice");
    facts.analysis->fallback = false;
    facts.analysis->count = 8193;
    require(!methods::validate(facts, morphology_fact_parameters, extent),
            "I02 sampling cannot exceed the actual lattice cardinality");
}
inline void morphology_identity_fact_cases() {
    methods::MorphologyFacts facts{
        .reason = methods::MorphologyReason::no_effect,
        .counts = {.eligible = 1, .evaluated = 1},
        .resolved_radius = 8,
        .sigma = 4,
        .gaussian_radius = 12,
        .analysis =
            methods::MorphologyAnalysis{
                .stride = 1,
                .count = 1,
                .analysis_fill = 0.4,
                .target = 0.4,
                .background_q10 = 0.4,
                .background_q50 = 0.4,
                .background_q90 = 0.4,
                .background_min = 0.4,
                .background_max = 0.4,
            },
    };
    require(methods::validate(facts, morphology_fact_parameters, morphology_singleton_extent),
            "I02 source-relative singleton completion retains its exact constant field");
    facts.counts = {.eligible = 1, .protected_samples = 15, .evaluated = 1};
    require(!methods::validate(facts), "active I02 rejects insufficient eligible samples");
    facts.counts = {.eligible = 1, .evaluated = 1};
    facts.max_gain = 2;
    facts.gain_capped_samples = 1;
    require(!methods::validate(facts),
            "I02 cannot brighten when its target is below every denominator");
    facts.min_gain = 2;
    facts.analysis = methods::MorphologyAnalysis{.stride = 1, .count = 1, .target = 0.1};
    require(methods::validate(facts, methods::MorphologyParameters{.target = 0.1},
                              morphology_singleton_extent),
            "an active black I02 field may attain the gain cap without changing samples");
    facts.min_gain = 1;
    facts.max_gain = 1;
    facts.gain_capped_samples = 0;
    require(!methods::validate(facts, methods::MorphologyParameters{.target = 0.1},
                               morphology_singleton_extent),
            "I02 gain observations obey bounds from its retained full-field background range");
}
inline void morphology_quantile_range_cases() {
    methods::MorphologyAnalysis analysis{
        .stride = 1,
        .count = 2,
        .analysis_fill = 0.7,
        .target = 0.8,
        .background_q10 = 0.3,
        .background_q50 = 0.3,
        .background_q90 = 0.8,
        .background_min = 0.2,
        .background_max = 0.9,
    };
    require(methods::validate(analysis),
            "sample extrema need not cover the retained full-field range");
    const methods::MorphologyFacts facts{
        .disposition = methods::Disposition::applied,
        .reason = methods::MorphologyReason::none,
        .counts = {.eligible = 2, .evaluated = 2, .changed = 1},
        .resolved_radius = 8,
        .sigma = 4,
        .gaussian_radius = 12,
        .analysis = analysis,
        .gain_capped_samples = 1,
        .min_gain = 1,
        .max_gain = 2,
    };
    require(!methods::validate(facts),
            "complete measurements must agree with an attained minimum and maximum rank");
    analysis.background_q50 = 0.4;
    require(!methods::validate(analysis), "equal nearest-rank indices require equal quantiles");
}
inline void morphology_fact_cases() {
    morphology_completion_fact_cases();
    morphology_bypass_fact_cases();
    morphology_sampling_fact_cases();
    morphology_identity_fact_cases();
    morphology_quantile_range_cases();
    morphology_attained_gain_cases();
    morphology_full_gain_range_cases();
    morphology_source_count_cases();
    morphology_quantile_count_cases();
    morphology_sample_gain_range_cases();
    morphology_unit_gain_count_cases();
}
} // namespace docenhance::tests
