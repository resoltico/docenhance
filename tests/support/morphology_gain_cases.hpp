// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "morphology_fixture.hpp"
#include "require.hpp"

namespace docenhance::tests {
inline void morphology_attained_gain_cases() {
    auto facts = completed_morphology_facts();
    if (!facts.analysis) {
        require(false, "the completed I02 fixture owns analysis facts");
        return;
    }
    facts.counts = {.eligible = 255, .protected_samples = 1, .evaluated = 255, .changed = 100};
    facts.analysis->count = 255;
    facts.min_gain = 1.1;
    require(!methods::validate(facts),
            "I02 source target has an attained eligible unit-gain quantile");
    facts.min_gain = 1;
    require(methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "protected full-field extrema retain bounds rather than forced gain equalities");
    facts.analysis->target = 0.9;
    facts.min_gain = 1.1;
    require(methods::validate(facts, methods::MorphologyParameters{.target = 0.9},
                              morphology_fact_extent),
            "a higher explicit I02 target need not attain unit gain");
    facts.min_gain = 1.2;
    require(!methods::validate(facts, methods::MorphologyParameters{.target = 0.9},
                               morphology_fact_extent),
            "the eligible background quantile must lie inside the observed gain range");
    facts = black_morphology_facts();
    constexpr image::Extent extent{.width = 4, .height = 4};
    require(methods::validate(facts, methods::MorphologyParameters{.target = 1}, extent),
            "an all-black I02 field can cap every gain without changing destinations");
    facts.gain_capped_samples = 15;
    require(!methods::validate(facts, methods::MorphologyParameters{.target = 1}, extent),
            "I02 minimum at the cap requires every evaluated gain capped");
}
inline void morphology_full_gain_range_cases() {
    auto facts = completed_morphology_facts();
    if (!facts.analysis) {
        require(false, "the completed I02 fixture owns analysis facts");
        return;
    }
    facts.analysis->target = 1;
    facts.min_gain = 1.2;
    facts.gain_capped_samples = 128;
    require(!methods::validate(facts, methods::MorphologyParameters{.target = 1},
                               morphology_fact_extent),
            "unprotected I02 minimum gain is attained at the global maximum background");
    facts.min_gain = 1.0 / 0.9;
    require(methods::validate(facts, methods::MorphologyParameters{.target = 1},
                              morphology_fact_extent),
            "unprotected I02 gain extrema agree with the existing ordered scalar ratio");
    facts.analysis->target = 0.8;
    facts.min_gain = 1;
    facts.analysis->background_min = 0.5;
    facts.analysis->background_q10 = 0.6;
    facts.analysis->background_q50 = 0.7;
    facts.max_gain = 1.5;
    facts.gain_capped_samples = 0;
    require(!methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "unprotected I02 maximum gain is attained at the global minimum background");
    facts.max_gain = 0.8 / 0.5;
    require(methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "uncapped full-field maximum gain retains the exact ordered ratio");
}
inline void morphology_source_count_cases() {
    auto facts = completed_morphology_facts();
    if (!facts.analysis) {
        require(false, "the completed I02 fixture owns analysis facts");
        return;
    }
    facts.analysis->analysis_fill = 0;
    require(!methods::validate(facts), "zero source Y90 limits the number of possible I02 writes");
    facts.counts.changed = 25;
    require(methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "zero-source and unit-gain quantile groups may overlap and must not be summed");
    facts.saturated_samples = 26;
    require(!methods::validate(facts), "zero source samples cannot produce I02 saturation");
    facts.analysis->analysis_fill = 1;
    facts.analysis->target = 1;
    facts.min_gain = 1.0 / 0.9;
    facts.gain_capped_samples = 128;
    facts.counts.changed = 240;
    facts.saturated_samples = 240;
    require(!methods::validate(facts), "unit source samples cannot produce I02 luminance writes");
    facts.counts.changed = 230;
    require(
        methods::validate(facts, methods::MorphologyParameters{.target = 1},
                          morphology_fact_extent),
        "source unit samples may saturate without a write or a source-to-background range bound");
}
inline void morphology_quantile_count_cases() {
    auto facts = completed_morphology_facts();
    facts.gain_capped_samples = 1;
    require(!methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "the lower background quantile supplies an actual capped-count lower bound");
    facts.gain_capped_samples = 240;
    require(!methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "the median background supplies an actual capped-count upper bound");
    facts.gain_capped_samples = 26;
    require(methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "I02 cap-count admission includes the exact lower nearest-rank boundary");
    facts.gain_capped_samples = 127;
    require(methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "I02 cap-count admission includes the exact upper nearest-rank boundary");
    facts.gain_capped_samples = 64;
    facts.counts.changed = 231;
    require(!methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "attained unit-gain samples limit I02 writes independently of source quantiles");
    facts.counts.changed = 230;
    require(methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "I02 write admission includes the actual unit-gain rank boundary");
    facts.saturated_samples = 231;
    require(!methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "attained unit-gain samples cannot saturate");
    facts.saturated_samples = 230;
    require(methods::validate(facts, morphology_fact_parameters, morphology_fact_extent),
            "I02 saturation admission includes the actual unit-gain rank boundary");
}
inline void morphology_sample_gain_range_cases() {
    auto facts = completed_morphology_facts();
    if (!facts.analysis) {
        require(false, "the completed I02 fixture owns analysis facts");
        return;
    }
    facts.counts = {
        .eligible = 254,
        .protected_samples = 2,
        .evaluated = 254,
        .changed = 100,
    };
    facts.analysis->count = 254;
    facts.analysis->target = 0.9;
    facts.analysis->background_q10 = 0.6;
    facts.analysis->background_q50 = 0.7;
    facts.min_gain = 1.1;
    facts.max_gain = 1.4;
    facts.gain_capped_samples = 0;
    require(!methods::validate(facts, methods::MorphologyParameters{.target = 0.9},
                               morphology_fact_extent),
            "eligible q10 must be represented even when protected extrema loosen the outer gain "
            "bounds");
    facts.max_gain = 0.9 / 0.6;
    require(
        methods::validate(facts, methods::MorphologyParameters{.target = 0.9},
                          morphology_fact_extent),
        "protected full-field extrema allow an attained sampled gain range inside their bounds");
}
inline void morphology_unit_gain_count_cases() {
    auto facts = completed_morphology_facts();
    if (!facts.analysis) {
        require(false, "the completed I02 fixture owns analysis facts");
        return;
    }
    facts.counts = {
        .eligible = 255,
        .protected_samples = 1,
        .evaluated = 255,
        .changed = 255,
    };
    facts.analysis->count = 255;
    facts.analysis->target = 0.9;
    require(!methods::validate(facts),
            "an attained unit minimum gain prevents at least one I02 write");
    facts.counts.changed = 254;
    require(methods::validate(facts, methods::MorphologyParameters{.target = 0.9},
                              morphology_fact_extent),
            "I02 may change every evaluated sample except its attained unit-gain point");
    facts.saturated_samples = 255;
    require(!methods::validate(facts),
            "an attained unit minimum gain prevents at least one saturation");
    facts.saturated_samples = 254;
    require(methods::validate(facts, methods::MorphologyParameters{.target = 0.9},
                              morphology_fact_extent),
            "I02 saturation admission includes the attained unit-gain boundary");
}
} // namespace docenhance::tests
