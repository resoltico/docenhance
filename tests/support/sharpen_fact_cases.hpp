// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "require.hpp"

#include <cmath>
#include <limits>

namespace docenhance::tests {
inline void sharpen_basic_fact_cases() {
    methods::UnsharpFacts facts{
        .disposition = methods::Disposition::applied,
        .reason = methods::UnsharpReason::none,
        .counts = {.eligible = 8, .protected_samples = 2, .evaluated = 8, .changed = 2},
        .context_samples = 10,
        .corrected_samples = 4,
        .pre_clamp = methods::ExcursionRange{.low = -0.1, .high = 1.1},
        .clipped_low_samples = 1,
        .clipped_high_samples = 1,
    };
    require(methods::validate(facts, methods::UnsharpParameters{}),
            "unsharp facts admit complete eligible work and protected context");
    require(!methods::validate(facts, methods::UnsharpParameters{.amount = 0}),
            "zero amount cannot claim correction work");
    facts.context_samples = 8;
    require(!methods::validate(facts), "unsharp context includes protected neighbors");
    facts.context_samples = 10;
    facts.corrected_samples = 1;
    require(!methods::validate(facts), "changed and clipped samples need actual corrections");
    facts = {
        .reason = methods::UnsharpReason::no_effect,
        .counts = {.eligible = 1, .protected_samples = 9, .evaluated = 1},
        .context_samples = 10,
        .corrected_samples = 1,
        .pre_clamp = methods::ExcursionRange{.low = -0.1, .high = -0.1},
        .clipped_low_samples = 1,
    };
    require(methods::validate(facts, methods::UnsharpParameters{}),
            "a corrected black candidate may clip back without a write");
    facts.pre_clamp = methods::ExcursionRange{.low = -0.1, .high = 0.5};
    require(!methods::validate(facts), "one evaluated value has equal raw extrema");
    facts = {
        .disposition = methods::Disposition::applied,
        .reason = methods::UnsharpReason::none,
        .counts = {.eligible = 1, .evaluated = 1, .changed = 1},
        .context_samples = 1,
        .corrected_samples = 1,
        .pre_clamp = methods::ExcursionRange{.low = 0.25, .high = 0.25},
    };
    require(!methods::validate(facts), "a singleton Gaussian context is an exact constant");
    facts = {
        .reason = methods::UnsharpReason::no_eligible_samples,
        .counts = {.protected_samples = 10},
    };
    require(methods::validate(facts, methods::UnsharpParameters{}),
            "fully protected sharpening performs no preparation");
    require(!methods::validate(facts, methods::UnsharpParameters{.amount = 0}),
            "zero amount keeps its defined reason precedence");
    facts.reason = methods::UnsharpReason::zero_amount;
    require(methods::validate(facts, methods::UnsharpParameters{.amount = 0}),
            "zero amount remains valid on a fully protected canvas");
}

inline void sharpen_correction_fact_cases() {
    methods::UnsharpFacts facts{
        .reason = methods::UnsharpReason::no_effect,
        .counts = {.eligible = 1, .protected_samples = 9, .evaluated = 1},
        .context_samples = 10,
        .corrected_samples = 1,
        .pre_clamp = methods::ExcursionRange{.low = 0.25, .high = 0.25},
    };
    require(!methods::validate(facts), "a non-clipped correction requires a write");
    facts.disposition = methods::Disposition::applied;
    facts.reason = methods::UnsharpReason::none;
    facts.counts.changed = 1;
    require(methods::validate(facts), "a non-clipped corrected destination records its write");
}

inline void sharpen_cardinality_fact_cases() {
    methods::UnsharpFacts facts{
        .disposition = methods::Disposition::applied,
        .reason = methods::UnsharpReason::none,
        .counts = {.eligible = 8, .protected_samples = 2, .evaluated = 8, .changed = 8},
        .context_samples = 10,
        .corrected_samples = 8,
        .pre_clamp = methods::ExcursionRange{.low = -0.1, .high = -0.1},
        .clipped_low_samples = 1,
    };
    require(!methods::validate(facts), "entirely negative raw values are all low-clipped");
    facts.clipped_low_samples = 8;
    require(methods::validate(facts), "all-negative extrema agree with all low clipping");
    facts.pre_clamp = methods::ExcursionRange{.low = 1.1, .high = 1.1};
    facts.clipped_low_samples = 0;
    facts.clipped_high_samples = 1;
    require(!methods::validate(facts), "entirely above-unit raw values are all high-clipped");
    facts.clipped_high_samples = 8;
    require(methods::validate(facts), "all-high extrema agree with all high clipping");
    facts.pre_clamp = methods::ExcursionRange{.low = 1, .high = 1.1};
    require(!methods::validate(facts), "an attained unit endpoint is not high-clipped");
    facts.clipped_high_samples = 7;
    require(methods::validate(facts), "strict high clipping excludes equality at one");
    facts.pre_clamp = methods::ExcursionRange{.low = -0.1, .high = 0};
    facts.clipped_high_samples = 0;
    facts.clipped_low_samples = 8;
    require(!methods::validate(facts), "an attained zero endpoint is not low-clipped");
    facts.clipped_low_samples = 7;
    require(methods::validate(facts), "strict low clipping excludes equality at zero");
}

inline void sharpen_envelope_fact_cases() {
    methods::UnsharpFacts facts{
        .disposition = methods::Disposition::applied,
        .reason = methods::UnsharpReason::none,
        .counts = {.eligible = 8, .protected_samples = 2, .evaluated = 8, .changed = 8},
        .context_samples = 10,
        .corrected_samples = 8,
        .pre_clamp = methods::ExcursionRange{.low = -0.5, .high = 1.5},
        .clipped_low_samples = 1,
        .clipped_high_samples = 1,
    };
    require(!methods::validate(facts, methods::UnsharpParameters{}),
            "threshold tightens the contextual raw envelope");
    constexpr double low = -0.4980392156862745;
    constexpr double high = 1.4980392156862745;
    facts.pre_clamp = methods::ExcursionRange{.low = low, .high = high};
    require(methods::validate(facts, methods::UnsharpParameters{}),
            "exact threshold-aware raw envelope endpoints are admitted");
    facts.pre_clamp = methods::ExcursionRange{
        .low = std::nextafter(low, 0.0),
        .high = std::nextafter(high, 1.0),
    };
    require(methods::validate(facts, methods::UnsharpParameters{}),
            "raw extrema immediately within the envelope are admitted");
    facts.pre_clamp = methods::ExcursionRange{
        .low = std::nextafter(low, -std::numeric_limits<double>::infinity()),
        .high = high,
    };
    require(!methods::validate(facts, methods::UnsharpParameters{}),
            "one representable value below the raw envelope is refused");
    facts.pre_clamp = methods::ExcursionRange{
        .low = low,
        .high = std::nextafter(high, std::numeric_limits<double>::infinity()),
    };
    require(!methods::validate(facts, methods::UnsharpParameters{}),
            "one representable value above the raw envelope is refused");
    facts.pre_clamp = methods::ExcursionRange{.low = low, .high = high};
    require(!methods::validate(facts, methods::UnsharpParameters{.threshold = 20}),
            "the contextual envelope follows the actual requested threshold");
}
inline void sharpen_fact_cases() {
    sharpen_basic_fact_cases();
    sharpen_correction_fact_cases();
    sharpen_cardinality_fact_cases();
    sharpen_envelope_fact_cases();
}
} // namespace docenhance::tests
