// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "require.hpp"

#include <cmath>
#include <limits>

namespace docenhance::tests {
inline void gamma_fact_cases() {
    methods::GammaFacts facts{
        .disposition = methods::Disposition::applied,
        .reason = methods::GammaReason::none,
        .counts = {.eligible = 8, .evaluated = 8, .changed = 3},
    };
    require(methods::validate(facts, methods::GammaParameters{}),
            "gamma facts admit complete active work");
    require(!methods::validate(facts, methods::GammaParameters{.gamma = 1}),
            "identity gamma cannot report active changes");
    facts.disposition = methods::Disposition::skipped;
    require(!methods::validate(facts), "contrast cannot claim automatic skipping");
    facts = {.reason = methods::GammaReason::identity_gamma, .counts = {.eligible = 8}};
    require(methods::validate(facts, methods::GammaParameters{.gamma = 1}),
            "identity gamma has no evaluated candidates");
    facts.counts.evaluated = 1;
    require(!methods::validate(facts), "identity gamma cannot claim candidate work");
    facts = {
        .reason = methods::GammaReason::no_eligible_samples,
        .counts = {.protected_samples = 8},
    };
    require(methods::validate(facts, methods::GammaParameters{}),
            "fully protected gamma has truthful empty work");
    require(!methods::validate(facts, methods::GammaParameters{.blend = 0}),
            "zero blend keeps its defined reason precedence");
}

inline void levels_fact_cases() {
    methods::LevelsFacts facts{
        .disposition = methods::Disposition::applied,
        .reason = methods::LevelsReason::none,
        .counts = {.eligible = 8, .evaluated = 8, .changed = 3},
        .levels = methods::LevelsRange{.low = 0.1, .high = 0.9},
    };
    require(methods::validate(facts, methods::LevelsParameters{}),
            "levels facts admit complete active mapping");
    facts.clipped_low_samples = 1;
    facts.clipped_high_samples = 1;
    require(!methods::validate(facts, methods::LevelsParameters{}),
            "nearest-rank endpoints exclude strict tails in an eight-sample canvas");
    facts.clipped_high_samples = 0;
    require(!methods::validate(facts, methods::LevelsParameters{}),
            "the selected minimum cannot have a strictly lower sample");
    facts.clipped_low_samples = 0;
    facts.clipped_high_samples = 1;
    require(!methods::validate(facts, methods::LevelsParameters{}),
            "the selected maximum cannot have a strictly higher sample");
    facts.clipped_high_samples = 0;
    facts.levels = methods::LevelsRange{.low = 0, .high = 1};
    require(!methods::validate(facts), "unit levels bounds cannot clip or change unit samples");
    facts = {
        .reason = methods::LevelsReason::insufficient_dynamic_range,
        .counts = {.eligible = 8},
        .levels =
            methods::LevelsRange{
                .low = 0,
                .high = std::nextafter(methods::levels_minimum_range, 0.0),
            },
    };
    require(methods::validate(facts), "strictly flat levels retain measured bounds");
    facts.levels = methods::LevelsRange{.low = 0, .high = methods::levels_minimum_range};
    require(!methods::validate(facts), "the exact levels range boundary is active");
    facts.reason = methods::LevelsReason::no_effect;
    facts.counts.evaluated = 8;
    require(methods::validate(facts), "an active levels mapping may leave all values unchanged");
    facts.clipped_low_samples = 1;
    require(!methods::validate(facts), "unit input cannot lie below a zero lower bound");
    facts = {.reason = methods::LevelsReason::zero_blend, .counts = {.eligible = 8}};
    require(methods::validate(facts, methods::LevelsParameters{.blend = 0}),
            "zero-blend levels claim no measurement or mapping");
    facts.levels = methods::LevelsRange{};
    require(!methods::validate(facts), "bypassed levels cannot invent fitted bounds");
}

inline void levels_rank_fact_cases() {
    methods::LevelsFacts facts{
        .reason = methods::LevelsReason::no_effect,
        .counts = {.eligible = 1, .evaluated = 1},
        .levels = methods::LevelsRange{.low = 0, .high = 1},
    };
    require(!methods::validate(facts, methods::LevelsParameters{}),
            "coincident percentile ranks cannot produce unequal fitted bounds");
    facts = {
        .reason = methods::LevelsReason::insufficient_dynamic_range,
        .counts = {.eligible = 1},
        .levels = methods::LevelsRange{.low = 0.5, .high = 0.5},
    };
    require(methods::validate(facts, methods::LevelsParameters{}),
            "one measured sample has a valid flat fit");
    facts = {
        .reason = methods::LevelsReason::no_effect,
        .counts = {.eligible = 8, .evaluated = 8},
        .levels = methods::LevelsRange{.low = 0.1, .high = 0.9},
    };
    require(methods::validate(facts), "basic facts do not duplicate the request context");
    require(!methods::validate(facts, methods::LevelsParameters{}),
            "full-blend nonidentity levels must write an attained fitted endpoint");
    facts.disposition = methods::Disposition::applied;
    facts.reason = methods::LevelsReason::none;
    facts.counts.changed = 1;
    require(!methods::validate(facts, methods::LevelsParameters{}),
            "two interior fitted endpoints require two distinct writes at full blend");
    facts.counts.changed = 2;
    require(methods::validate(facts, methods::LevelsParameters{}),
            "two interior fitted endpoint writes meet the necessary lower bound");
    facts.levels = methods::LevelsRange{.low = 0, .high = 0.9};
    facts.counts.changed = 1;
    require(methods::validate(facts, methods::LevelsParameters{}),
            "one interior fitted endpoint requires only one write");
    facts = {
        .reason = methods::LevelsReason::no_effect,
        .counts = {.eligible = 8, .evaluated = 8},
        .levels = methods::LevelsRange{.low = 0, .high = 1},
    };
    require(methods::validate(facts, methods::LevelsParameters{}),
            "full-blend unit levels bounds remain an exact identity");
    facts = {
        .reason = methods::LevelsReason::no_effect,
        .counts = {.eligible = 201, .evaluated = 201},
        .levels = methods::LevelsRange{.low = 0.1, .high = 0.9},
        .clipped_low_samples = 1,
        .clipped_high_samples = 1,
    };
    require(
        methods::validate(
            facts, methods::LevelsParameters{.blend = std::numeric_limits<double>::denorm_min()}),
        "strict tails need not change samples under a tiny nonzero blend");
}

inline void clahe_fact_cases() {
    methods::ClaheFacts facts{
        .disposition = methods::Disposition::applied,
        .reason = methods::ClaheReason::none,
        .counts = {.eligible = 16384, .evaluated = 16384, .changed = 3},
        .identity_tiles = 4,
    };
    require(methods::validate(facts, methods::ClaheParameters{}),
            "CLAHE facts admit an applicable contextual grid");
    const auto active = facts;
    facts.counts = {.eligible = 8, .protected_samples = 16376, .evaluated = 8, .changed = 1};
    facts.identity_tiles = 63;
    require(!methods::validate(facts, methods::ClaheParameters{}),
            "sparse eligibility cannot populate one nonidentity contextual tile");
    facts.counts = {.eligible = 16, .protected_samples = 16368, .evaluated = 16, .changed = 1};
    require(methods::validate(facts, methods::ClaheParameters{}),
            "sixteen eligible samples can populate exactly one contextual tile");
    facts = active;
    facts.identity_tiles = 64;
    require(!methods::validate(facts, methods::ClaheParameters{}),
            "an entirely identity grid cannot report changes");
    facts.disposition = methods::Disposition::no_change;
    facts.reason = methods::ClaheReason::no_effect;
    facts.counts.changed = 0;
    require(methods::validate(facts, methods::ClaheParameters{}),
            "identity maps preserve exact no-change observations");
    facts.counts = {.eligible = 8, .evaluated = 8};
    require(!methods::validate(facts, methods::ClaheParameters{}),
            "active CLAHE cannot claim an undersized grid");
    facts = {.reason = methods::ClaheReason::zero_blend, .counts = {.eligible = 8}};
    require(methods::validate(facts, methods::ClaheParameters{.blend = 0}),
            "zero blend bypasses pixel-dependent tile applicability");
    facts.identity_tiles = 1;
    require(!methods::validate(facts), "bypassed CLAHE cannot invent measured maps");
}
inline void contrast_fact_cases() {
    gamma_fact_cases();
    levels_fact_cases();
    levels_rank_fact_cases();
    clahe_fact_cases();
}
} // namespace docenhance::tests
