// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "require.hpp"

namespace docenhance::tests {
inline void nlm_table_cases() {
    const auto defaults = methods::Nlm::create().value();
    require(methods::nlm_weight_entries(defaults) == 113337,
            "default NLM cutoff follows the specified ordered expression");
    const auto minimum = methods::Nlm::create({.h = 0.1, .patch = 3, .search = 7}).value();
    require(methods::nlm_weight_entries(minimum) == 694,
            "minimum NLM strength and patch retain a positive cutoff");
    const auto maximum = methods::Nlm::create({.h = 25, .patch = 15, .search = 41}).value();
    require(methods::nlm_weight_entries(maximum) == 4336875,
            "maximum NLM cutoff stays within the reviewed table domain");
    const auto ordered = methods::Nlm::create({.h = 25, .patch = 13, .search = 21}).value();
    require(methods::nlm_weight_entries(ordered) == 3257476,
            "NLM cutoff preserves rounding before ceil rather than integer reassociation");
}
inline void nlm_no_effect_cases() {
    methods::NlmFacts facts{
        .reason = methods::NlmReason::no_effect,
        .counts = {.eligible = 441, .evaluated = 441},
        .weight_table_entries = methods::nlm_weight_entries(methods::Nlm::create().value()),
    };
    require(methods::validate(facts, methods::NlmParameters{}),
            "an active all-Q0 no-effect report retains complete evaluation and its table");
    facts.weight_table_entries = 0;
    require(!methods::validate(facts, methods::NlmParameters{}),
            "active NLM no-effect work cannot omit its table");
    facts.weight_table_entries = methods::nlm_weight_entries(methods::Nlm::create().value());
    facts.counts.evaluated = 0;
    require(!methods::validate(facts, methods::NlmParameters{}),
            "active NLM no-effect work cannot claim bypass evaluation");
}
inline void nlm_fact_cases() {
    methods::NlmFacts facts{
        .disposition = methods::Disposition::applied,
        .reason = methods::NlmReason::none,
        .counts = {.eligible = 441, .evaluated = 441, .changed = 100},
        .corrected_samples = 120,
        .weight_table_entries = methods::nlm_weight_entries(methods::Nlm::create().value()),
    };
    require(methods::validate(facts, methods::NlmParameters{}),
            "NLM facts agree with the actual owned table-size helper");
    facts.weight_table_entries = 1;
    require(methods::validate(facts), "basic Count metadata does not duplicate request formulae");
    require(!methods::validate(facts, methods::NlmParameters{}),
            "a one-entry table cannot claim the default NLM request");
    facts.weight_table_entries = 40000000;
    require(!methods::validate(facts, methods::NlmParameters{}),
            "a broad Count ceiling cannot substitute for exact request agreement");
    facts.weight_table_entries = methods::nlm_weight_entries(methods::Nlm::create().value());
    facts.counts = {.eligible = 440, .evaluated = 440, .changed = 100};
    require(!methods::validate(facts, methods::NlmParameters{}),
            "active search has a necessary canvas-area minimum");
    facts = {.reason = methods::NlmReason::zero_blend, .counts = {.eligible = 1}};
    require(methods::validate(facts, methods::NlmParameters{.blend = 0}),
            "zero-blend NLM bypasses pixel-dependent search and table work");
    facts.weight_table_entries = 1;
    require(!methods::validate(facts), "a bypass cannot claim a prepared NLM table");
    facts = {
        .reason = methods::NlmReason::no_eligible_samples,
        .counts = {.protected_samples = 1},
    };
    require(methods::validate(facts, methods::NlmParameters{}),
            "fully protected NLM claims no allocation or candidate work");
    require(!methods::validate(facts, methods::NlmParameters{.blend = 0}),
            "zero blend precedes the no-eligible NLM reason");
    require(!methods::validate(facts, methods::NlmParameters{.patch = 15, .search = 7}),
            "invalid cross-field parameters stay invalid on an NLM bypass");
}
inline void nlm_cases() {
    nlm_table_cases();
    nlm_fact_cases();
    nlm_no_effect_cases();
}
} // namespace docenhance::tests
