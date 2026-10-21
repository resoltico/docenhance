// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "require.hpp"

#include <cmath>
#include <limits>

namespace docenhance::tests {
inline methods::Tvl1Facts completed_tv_facts() {
    return {
        .disposition = methods::Disposition::applied,
        .reason = methods::Tvl1Reason::none,
        .counts = {.eligible = 8, .evaluated = 8, .changed = 2},
        .context_samples = 8,
        .corrected_samples = 3,
        .solver =
            methods::Tvl1Solution{
                .iterations = 30,
                .stop = methods::Tvl1Stop::tolerance_met,
                .passing_checkpoints = 2,
                .primal_update = 1e-6,
                .dual_update = 2e-6,
                .objective_start = 8,
                .objective_end = 7,
            },
    };
}
inline void tv_completion_fact_cases() {
    auto facts = completed_tv_facts();
    require(methods::validate(facts, methods::Tvl1Parameters{}),
            "TV completion retains its second tolerance checkpoint and objective facts");
    require(!methods::validate(facts, methods::Tvl1Parameters{.blend = 1}),
            "full TV blend writes every corrected non-clipped unit candidate");
    facts.corrected_samples = 2;
    require(methods::validate(facts, methods::Tvl1Parameters{.blend = 1}),
            "full TV blend admits matching correction and write counts");
    facts.solver = methods::Tvl1Solution{
        .iterations = 30,
        .stop = methods::Tvl1Stop::tolerance_met,
        .passing_checkpoints = 2,
        .primal_update = 1e-4,
        .dual_update = 0,
        .objective_start = 8,
        .objective_end = 7,
    };
    require(!methods::validate(facts, methods::Tvl1Parameters{}),
            "tolerance completion must agree with the requested stopping bound");
    facts = {.reason = methods::Tvl1Reason::zero_blend, .counts = {.eligible = 8}};
    require(methods::validate(facts, methods::Tvl1Parameters{.blend = 0}),
            "zero TV blend performs no solver preparation");
    facts.solver = methods::Tvl1Solution{};
    require(!methods::validate(facts), "a TV bypass cannot claim a solver result");
}
inline void tv_limit_fact_cases() {
    auto facts = completed_tv_facts();
    facts.solver = methods::Tvl1Solution{
        .iterations = 10,
        .stop = methods::Tvl1Stop::iteration_limit,
        .primal_update = 0.1,
        .dual_update = 0.1,
        .objective_start = 8,
        .objective_end = 7,
    };
    require(methods::validate(facts, methods::Tvl1Parameters{.iterations = 10}),
            "short TV caps finish without inventing a checkpoint pass");
    require(!methods::validate(facts, methods::Tvl1Parameters{.iterations = 20}),
            "iteration-limit completion reaches the actual requested cap");
    facts.solver = methods::Tvl1Solution{
        .iterations = 20,
        .stop = methods::Tvl1Stop::iteration_limit,
        .passing_checkpoints = 1,
        .primal_update = 1e-6,
        .dual_update = 1e-6,
        .objective_start = 8,
        .objective_end = 7,
    };
    require(methods::validate(facts, methods::Tvl1Parameters{.iterations = 20}),
            "a single passing checkpoint does not claim tolerance completion");
    facts.solver = methods::Tvl1Solution{
        .iterations = 20,
        .stop = methods::Tvl1Stop::iteration_limit,
        .primal_update = 1e-6,
        .dual_update = 1e-6,
        .objective_start = 8,
        .objective_end = 7,
    };
    require(!methods::validate(facts, methods::Tvl1Parameters{.iterations = 20}),
            "checkpoint update facts and the pass streak must agree");
    facts = completed_tv_facts();
    require(methods::validate(facts, methods::Tvl1Parameters{.iterations = 30}),
            "a second successful checkpoint at the cap still wins tolerance completion");
}
inline void tv_noncheckpoint_fact_cases() {
    auto facts = completed_tv_facts();
    facts.solver = methods::Tvl1Solution{
        .iterations = 29,
        .stop = methods::Tvl1Stop::iteration_limit,
        .passing_checkpoints = 1,
        .primal_update = 0.1,
        .dual_update = 0.1,
        .objective_start = 8,
        .objective_end = 9,
    };
    require(methods::validate(facts, methods::Tvl1Parameters{.iterations = 29}),
            "noncheckpoint caps retain the previous pass streak without objective monotonicity");
    facts.solver = methods::Tvl1Solution{
        .iterations = 29,
        .stop = methods::Tvl1Stop::tolerance_met,
        .passing_checkpoints = 2,
    };
    require(!methods::validate(facts), "tolerance cannot complete between checkpoints");
    facts = {.counts = {.protected_samples = 8}};
    require(methods::validate(facts, methods::Tvl1Parameters{}),
            "fully protected TV completion has no solver context or allocation facts");
    require(!methods::validate(facts, methods::Tvl1Parameters{.blend = 0}),
            "zero blend precedes the fully protected reason");
    facts = completed_tv_facts();
    facts.context_samples = 7;
    require(!methods::validate(facts), "TV solver context retains every entering sample");
}
inline void tv_singleton_fact_cases() {
    methods::Tvl1Facts facts{
        .reason = methods::Tvl1Reason::no_effect,
        .counts = {.eligible = 1, .evaluated = 1},
        .context_samples = 1,
        .solver =
            methods::Tvl1Solution{
                .iterations = 30,
                .stop = methods::Tvl1Stop::tolerance_met,
                .passing_checkpoints = 2,
            },
    };
    require(methods::validate(facts, methods::Tvl1Parameters{}),
            "a singleton TV field is an exact constant stopping at the first two checks");
    facts.solver = methods::Tvl1Solution{
        .iterations = 40,
        .stop = methods::Tvl1Stop::tolerance_met,
        .passing_checkpoints = 2,
    };
    require(!methods::validate(facts), "constant TV cannot delay its already-met stop");
    facts.solver = methods::Tvl1Solution{
        .iterations = 20,
        .stop = methods::Tvl1Stop::iteration_limit,
        .passing_checkpoints = 1,
    };
    require(methods::validate(facts, methods::Tvl1Parameters{.iterations = 20}),
            "a constant short-cap TV report retains one actual passing checkpoint");
}
inline void tv_objective_fact_cases() {
    auto facts = completed_tv_facts();
    if (!facts.solver) {
        require(false, "the completed TV fixture owns solver facts");
        return;
    }
    facts.solver->objective_start = 0;
    require(!methods::validate(facts),
            "zero initial TV objective certifies a constant field of any size");
    facts.solver->objective_start = 1e300;
    require(!methods::validate(facts), "initial TV objective has a finite field-size ceiling");
    facts.solver->objective_start = 8;
    facts.solver->objective_end = 1e300;
    require(!methods::validate(facts), "final TV objective has a finite field-size ceiling");
    facts.solver->objective_start = 16;
    facts.solver->objective_end = 32;
    require(methods::validate(facts, methods::Tvl1Parameters{}),
            "coarse integer objective admission ceilings include their exact boundary");
    facts.solver->objective_start = std::nextafter(16.0, 17.0);
    require(!methods::validate(facts), "initial objective rejects one ULP beyond its ceiling");
    facts.solver->objective_start = 8;
    facts.solver->objective_end = std::nextafter(32.0, 33.0);
    require(methods::validate(facts), "generic final objective uses the maximum lambda");
    require(!methods::validate(facts, methods::Tvl1Parameters{}),
            "contextual objective ceiling uses the selected lambda");
    facts.solver->objective_end = 176;
    require(methods::validate(facts), "generic final objective admits its integer ceiling");
    facts.solver->objective_end = std::nextafter(176.0, 177.0);
    require(!methods::validate(facts), "generic final objective rejects excess over its ceiling");
    facts.solver->objective_start = 8;
    facts.solver->objective_end = 7;
    facts.disposition = methods::Disposition::no_change;
    facts.reason = methods::Tvl1Reason::no_effect;
    facts.counts.changed = 0;
    facts.corrected_samples = 0;
    require(!methods::validate(facts),
            "without protection zero final corrections require identical full-field objectives");
    facts.solver->objective_end = facts.solver->objective_start;
    require(methods::validate(facts, methods::Tvl1Parameters{}),
            "equal initial and final fields do not promise zero last-iteration updates");
    facts.counts = {.eligible = 7, .protected_samples = 1, .evaluated = 7};
    facts.solver->objective_end = 7;
    require(methods::validate(facts, methods::Tvl1Parameters{}),
            "protected solver context may change despite zero eligible corrections");
    facts.counts = {.eligible = 8, .evaluated = 8};
    facts.solver = methods::Tvl1Solution{
        .iterations = 30,
        .stop = methods::Tvl1Stop::tolerance_met,
        .passing_checkpoints = 2,
        .objective_start = std::numeric_limits<double>::denorm_min(),
        .objective_end = std::numeric_limits<double>::denorm_min(),
    };
    require(methods::validate(facts, methods::Tvl1Parameters{}),
            "positive subnormal objectives must not become an epsilon-based constant test");
    facts.solver = methods::Tvl1Solution{
        .iterations = 30,
        .stop = methods::Tvl1Stop::tolerance_met,
        .passing_checkpoints = 2,
    };
    require(methods::validate(facts, methods::Tvl1Parameters{}),
            "a multi-sample constant TV field stops exactly at its first two checkpoints");
    facts.solver->iterations = 40;
    require(!methods::validate(facts), "multi-sample constant TV cannot delay completion");
}
inline void tvl1_fact_cases() {
    tv_completion_fact_cases();
    tv_limit_fact_cases();
    tv_noncheckpoint_fact_cases();
    tv_singleton_fact_cases();
    tv_objective_fact_cases();
}
} // namespace docenhance::tests
