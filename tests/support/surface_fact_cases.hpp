// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "require.hpp"
#include "surface_fixture.hpp"
#include "surface_population_cases.hpp"

#include <optional>

namespace docenhance::tests {
inline void surface_completion_fact_cases() {
    auto facts = completed_surface_facts();
    require(methods::validate(facts, surface_fact_parameters, surface_fact_extent),
            "I01 completed facts retain the actual fit measurements and application");
    require(methods::validate(facts,
                              methods::SurfaceParameters{.mode = methods::SurfaceMode::automatic},
                              surface_fact_extent),
            "I01 automatic application requires all six actual predicates");
    facts.auto_predicates.paper_fraction = false;
    require(!methods::validate(facts),
            "I01 predicate decisions must match their actual scalar owner");
    facts.auto_predicates.paper_fraction = true;
    facts.coverage = 0.9;
    require(!methods::validate(facts), "I01 coverage must equal its measured-cell fraction");
    facts.coverage = 1;
    facts.measured_cells = 0;
    facts.dark_cells = 16;
    facts.coverage = 0;
    require(!methods::validate(facts), "I01 dark exclusion cannot remove its own reference cell");
}
inline void surface_bypass_fact_cases() {
    constexpr image::Extent extent{.width = 40000000, .height = 1};
    methods::SurfaceFacts facts{
        .reason = methods::SurfaceReason::zero_strength,
        .counts = {.eligible = 40000000},
    };
    require(methods::validate(facts, methods::SurfaceParameters{.strength = 0, .cell = 8}, extent),
            "I01 zero strength finishes before a hypothetical oversized grid");
    facts.counts = {.protected_samples = 40000000};
    facts.reason = methods::SurfaceReason::no_eligible_samples;
    require(methods::validate(
                facts, methods::SurfaceParameters{.strength = 0, .max_gain = 1, .cell = 8}, extent),
            "I01 no eligible samples precedes zero strength and unit gain");
    facts.reason = methods::SurfaceReason::zero_strength;
    require(!methods::validate(facts), "I01 cannot reuse I02's zero-strength-first priority");
    facts.counts = {.eligible = 40000000};
    facts.reason = methods::SurfaceReason::unit_gain;
    require(methods::validate(facts, methods::SurfaceParameters{.max_gain = 1, .cell = 8}, extent),
            "I01 unit gain also bypasses grid resolution");
    require(!methods::validate(
                facts, methods::SurfaceParameters{.strength = 0, .max_gain = 1, .cell = 8}, extent),
            "I01 zero strength precedes unit gain when eligible pixels exist");
    facts.disposition = methods::Disposition::skipped;
    facts.reason = methods::SurfaceReason::insufficient_samples;
    facts.counts = {.eligible = 15, .protected_samples = 39999985};
    require(methods::validate(
                facts,
                methods::SurfaceParameters{.mode = methods::SurfaceMode::automatic, .cell = 8},
                extent),
            "I01 automatic insufficient samples finishes before any grid attempt");
    require(!methods::validate(facts, methods::SurfaceParameters{.cell = 8}, extent),
            "explicit I01 insufficiency is not a completed automatic skip");
    facts = completed_surface_facts();
    facts.counts = {.eligible = 40000000, .evaluated = 40000000, .changed = 2000};
    require(!methods::validate(facts, methods::SurfaceParameters{.cell = 8}, extent),
            "active oversized I01 grid admission refuses through nonallocating geometry");
}
inline void surface_predicate_fact_cases() {
    auto facts = completed_surface_facts();
    if (!facts.measurements) {
        require(false, "the completed I01 fixture owns measurements");
        return;
    }
    facts.measurements->background_q10 = 0.8;
    facts.measurements->background_q50 = 0.8;
    facts.measurements->variation = 0;
    facts.auto_predicates.background_variation = false;
    facts.counts.changed = 100;
    facts.gain_capped_samples = 100;
    facts.saturated_samples = 3;
    require(methods::validate(facts, surface_fact_parameters, surface_fact_extent),
            "explicit I01 application may retain a false automatic predicate");
    const methods::SurfaceParameters automatic{.mode = methods::SurfaceMode::automatic};
    require(!methods::validate(facts, automatic, surface_fact_extent),
            "automatic I01 cannot apply with a false predicate");
    facts.disposition = methods::Disposition::skipped;
    facts.reason = methods::SurfaceReason::automatic_predicates;
    facts.counts.evaluated = 0;
    facts.counts.changed = 0;
    facts.gain_capped_samples = 0;
    facts.saturated_samples = 0;
    facts.max_gain = 1;
    require(methods::validate(facts, automatic, surface_fact_extent),
            "automatic I01 skip retains a solved field without application counts");
    require(!methods::validate(facts, surface_fact_parameters, surface_fact_extent),
            "explicit I01 never becomes an automatic-predicate skip");
    facts = completed_surface_facts();
    facts.disposition = methods::Disposition::skipped;
    facts.reason = methods::SurfaceReason::insufficient_cells;
    facts.counts = {.eligible = 4096};
    facts.measured_cells = 4;
    facts.coverage = 0.25;
    facts.solver = std::nullopt;
    facts.measurements = std::nullopt;
    facts.auto_predicates = {.coverage = false};
    facts.gain_capped_samples = 0;
    facts.saturated_samples = 0;
    facts.max_gain = 1;
    require(methods::validate(facts, automatic, surface_fact_extent),
            "I01 insufficient-cell skip keeps coverage false and later predicates unevaluated");
    facts.auto_predicates.dark_fraction = false;
    require(!methods::validate(facts), "I01 unevaluated predicate must remain null, not false");
}
inline void surface_measurement_fact_cases() {
    auto facts = completed_surface_facts();
    if (!facts.measurements || !facts.solver) {
        require(false, "the completed I01 fixture owns solver and measurement facts");
        return;
    }
    facts.measurements->variation = 0.5;
    require(!methods::validate(facts), "I01 variation is the actual ordered quantile ratio");
    facts.measurements->variation = (0.8 - 0.3) / 0.8;
    facts.measurements->paper_fraction = 0.8;
    require(!methods::validate(facts), "I01 fractions must represent integer sampled populations");
    facts.measurements->paper_fraction = 0.75;
    facts.measurements->dark_fraction = 0.5;
    require(!methods::validate(facts), "I01 sampled paper and dark events are disjoint");
    facts.measurements->dark_fraction = 0.0625;
    facts.solver->tolerance = 1e300;
    require(!methods::validate(facts), "I01 tolerance cannot exceed a finite method RHS bound");
    facts.solver->residual = 0;
    facts.solver->tolerance = 1e-30;
    require(!methods::validate(facts), "I01 solver tolerance retains its actual RHS floor");
    facts.solver = methods::SolverReport{.iterations = 0, .residual = 1e-9, .tolerance = 1e-7};
    require(!methods::validate(facts),
            "I01 iteration zero retains a globally constant initialized field on every grid");
    const auto singleton = singleton_surface_facts();
    constexpr image::Extent extent{.width = 1, .height = 1};
    require(methods::validate(singleton, surface_fact_parameters, extent),
            "I01 log-fit then exp may differ from entering singleton Y by one ULP");
}
inline void surface_sampling_fact_cases() {
    auto facts = singleton_surface_facts();
    if (!facts.measurements) {
        require(false, "the singleton I01 fixture owns measurements");
        return;
    }
    facts.counts = {.eligible = 512, .protected_samples = 513, .evaluated = 512};
    facts.resolved_cell = 32;
    facts.cells = 33;
    facts.measured_cells = 32;
    facts.coverage = 32.0 / 33.0;
    facts.measurements->stride = 2;
    facts.measurements->count = 512;
    facts.measurements->fallback = true;
    facts.measurements->paper_fraction = 1;
    constexpr image::Extent extent{.width = 1025, .height = 1};
    const methods::SurfaceParameters parameters{.cell = 32};
    require(methods::validate(facts, parameters, extent),
            "I01 lattice fallback gathers all eligible points instead of I02's first sample");
    facts.measurements->count = 1;
    require(!methods::validate(facts, parameters, extent),
            "I01 fallback count must match its actual row-major sampled population");
}
inline void surface_fact_cases() {
    surface_completion_fact_cases();
    surface_bypass_fact_cases();
    surface_predicate_fact_cases();
    surface_measurement_fact_cases();
    surface_sampling_fact_cases();
    surface_reference_fact_cases();
    surface_cell_support_cases();
    surface_initial_field_cases();
    surface_joint_fraction_cases();
    surface_white_phase_cases();
}
} // namespace docenhance::tests
