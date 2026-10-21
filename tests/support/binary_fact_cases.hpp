// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "require.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace docenhance::tests {
inline void classification_fact_cases() {
    methods::ClassificationFacts facts{.black_samples = 3, .white_samples = 5};
    require(methods::validate(facts), "classification counts cover a nonempty canvas");
    facts.disposition = methods::Disposition::no_change;
    require(!methods::validate(facts), "terminal classification is completed application");
    facts.disposition = methods::Disposition::applied;
    facts.black_samples = image::source_pixels_max;
    require(!methods::validate(facts), "classification totals retain the source pixel ceiling");
    facts.black_samples = std::numeric_limits<std::uint64_t>::max();
    require(!methods::validate(facts), "classification total checks cannot wrap");
    require(!methods::validate(methods::ClassificationFacts{}),
            "an empty tally cannot fabricate completed classification");
}
inline void otsu_fact_cases() {
    methods::OtsuFacts facts;
    facts.black_samples = 3;
    facts.white_samples = 5;
    facts.threshold_bin = 100;
    require(methods::validate(facts), "nonfallback Otsu retains both fitted populations");
    facts.threshold_bin = methods::otsu_fallback_threshold;
    facts.single_bin_fallback = true;
    require(!methods::validate(facts), "single-bin fallback cannot classify both polarities");
    facts.black_samples = 8;
    facts.white_samples = 0;
    require(methods::validate(facts), "single-bin fallback may classify the whole canvas black");
    facts.black_samples = 0;
    facts.white_samples = 8;
    require(methods::validate(facts), "single-bin fallback may classify the whole canvas white");
    facts.threshold_bin = methods::otsu_fallback_threshold - 1;
    require(!methods::validate(facts), "fallback retains its exact frozen threshold");
    facts.threshold_bin = methods::otsu_fallback_threshold;
    facts.single_bin_fallback = false;
    require(!methods::validate(facts), "nonfallback Otsu needs nonempty classes on both sides");
    facts.black_samples = 3;
    facts.white_samples = 5;
    facts.threshold_bin = methods::otsu_histogram_bins - 1;
    require(!methods::validate(facts), "the last histogram bin is not a candidate threshold");
}
inline void terminal_method_fact_cases() {
    methods::FixedFacts fixed;
    fixed.white_samples = 8;
    require(methods::validate(fixed, methods::FixedThresholdParameters{}),
            "fixed classification may be entirely white");
    require(!methods::validate(fixed, methods::FixedThresholdParameters{.threshold = 1}),
            "unit fixed threshold classifies every admitted code black");
    fixed.white_samples = 0;
    fixed.black_samples = 8;
    require(methods::validate(fixed, methods::FixedThresholdParameters{.threshold = 1}),
            "unit fixed threshold has a truthful all-black tally");
    methods::SauvolaFacts local;
    local.white_samples = 8;
    require(methods::validate(local, methods::SauvolaParameters{}),
            "positive-k Sauvola may classify a positive constant white");
    require(!methods::validate(local, methods::SauvolaParameters{.k = 0}),
            "a finite local-mean test has a black global-minimum sample");
    require(!methods::validate(local, methods::SauvolaParameters{.k = 1e-20}),
            "tiny normal k still retains a black global-minimum sample after rounding");
    constexpr double boundary = 0x1p-54;
    require(!methods::validate(local, methods::SauvolaParameters{.k = boundary}),
            "the exact half-ULP k boundary rounds the minimum multiplier to one");
    local.white_samples = 0;
    local.black_samples = 8;
    require(methods::validate(local, methods::SauvolaParameters{.k = boundary}),
            "an all-black tally is truthful at the rounded local-mean boundary");
    local.white_samples = 8;
    local.black_samples = 0;
    require(methods::validate(
                local,
                methods::SauvolaParameters{
                    .k = std::nextafter(boundary, std::numeric_limits<double>::infinity()),
                }),
            "the next k above the boundary admits positive-constant white classification");
    local.white_samples = 7;
    local.black_samples = 1;
    require(methods::validate(local, methods::SauvolaParameters{.k = 0}),
            "local-mean classification retains a nonempty black population");
}
inline void binary_fact_cases() {
    classification_fact_cases();
    otsu_fact_cases();
    terminal_method_fact_cases();
}
} // namespace docenhance::tests
