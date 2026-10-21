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

#include <cmath>

namespace docenhance::tests {
inline void surface_reference_fact_cases() {
    auto facts = completed_surface_facts();
    facts.background_reference = 0.001;
    require(!methods::validate(facts), "I01 cell reference cannot precede its log-admission floor");
    const auto floor = std::exp(std::log(methods::illumination_floor));
    facts.background_reference = floor;
    require(methods::validate(facts, surface_fact_parameters, surface_fact_extent),
            "I01 reference admission preserves the actual log then exp floor");
    facts.background_reference = std::nextafter(floor, 0.0);
    require(!methods::validate(facts), "one ULP below I01 reference admission is refused");
}
inline void surface_cell_support_cases() {
    auto facts = completed_surface_facts();
    if (!facts.measurements) {
        require(false, "the completed I01 fixture owns measurements");
        return;
    }
    facts.counts = {.eligible = 16, .protected_samples = 4080, .evaluated = 16, .changed = 8};
    facts.measurements->count = 16;
    facts.gain_capped_samples = 4;
    facts.saturated_samples = 1;
    require(methods::validate(facts),
            "the support counterexample passes independent scalar fact bounds");
    require(!methods::validate(facts, surface_fact_parameters, surface_fact_extent),
            "sixteen full I01 cells require at least 1024 eligible observations");
    facts.counts = {.eligible = 1024, .protected_samples = 3072, .evaluated = 1024, .changed = 8};
    facts.measurements->count = 1024;
    facts.gain_capped_samples = 256;
    require(methods::validate(facts, surface_fact_parameters, surface_fact_extent),
            "I01 cell support admission includes the exact required-count boundary");
    facts = singleton_surface_facts();
    facts.counts = {.eligible = 17, .protected_samples = 16, .evaluated = 17};
    facts.resolved_cell = 32;
    facts.cells = 2;
    facts.measured_cells = 2;
    facts.measurements = methods::SurfaceMeasurements{
        .stride = 1,
        .count = 17,
        .target = 0.10000000000000002,
        .background_q10 = 0.10000000000000002,
        .background_q50 = 0.10000000000000002,
        .background_q90 = 0.10000000000000002,
        .luminance_q90 = 0.1,
        .paper_fraction = 1,
    };
    constexpr image::Extent extent{.width = 33, .height = 1};
    require(methods::validate(facts, methods::SurfaceParameters{.cell = 32}, extent),
            "a clipped singleton cell needs one observation, not an invented sixteen");
}
inline void surface_initial_field_cases() {
    auto facts = constant_initial_surface_facts();
    if (!facts.measurements || !facts.solver) {
        require(false, "the two-cell scalar witness owns measurement and solver facts");
        return;
    }
    constexpr image::Extent extent{.width = 9, .height = 8};
    const methods::SurfaceParameters parameters{.cell = 8};
    require(facts.solver->iterations == 0 && facts.solver->residual > 0,
            "the weighted-mean scalar witness has a nonzero already-small initial residual");
    require(methods::validate(facts, parameters, extent),
            "zero-iteration I01 can complete with constant background and nonzero small residual");
    facts.measurements->target = 0.2;
    const auto gain = 0.2 / facts.measurements->background_q90;
    facts.min_gain = gain;
    facts.max_gain = gain;
    facts.disposition = methods::Disposition::applied;
    facts.reason = methods::SurfaceReason::none;
    facts.counts.changed = 57;
    require(methods::validate(facts, methods::SurfaceParameters{.target = 0.2, .cell = 8}, extent),
            "zero solver iterations do not imply no application under a higher explicit target");
}
inline void surface_joint_fraction_cases() {
    auto facts = completed_surface_facts();
    if (!facts.measurements) {
        require(false, "the completed I01 fixture owns measurements");
        return;
    }
    facts.measurements->target = 0.95;
    facts.measurements->background_q10 = 0.8;
    facts.measurements->background_q50 = 0.9;
    facts.measurements->background_q90 = 0.95;
    facts.measurements->luminance_q90 = 0.35;
    facts.measurements->variation = (0.95 - 0.8) / 0.95;
    facts.gain_capped_samples = 100;
    require(!methods::validate(*facts.measurements),
            "separately sorted quantiles still constrain the possible joint paper and dark counts");
    require(
        !methods::validate(facts),
        "apparently passing all six scalar auto thresholds cannot rescue impossible populations");
    methods::SurfaceMeasurements measurements{
        .stride = 1,
        .count = 16,
        .target = 0.8,
        .background_q10 = 0.8,
        .background_q50 = 0.8,
        .background_q90 = 0.8,
        .luminance_q90 = 0.75 * 0.8,
    };
    require(methods::validate(measurements),
            "I01 dark exclusion is strict at its actual product threshold");
    measurements.luminance_q90 = std::nextafter(0.75 * 0.8, 0.0);
    require(!methods::validate(measurements),
            "one ULP below the dark threshold needs actual dark samples");
    measurements.luminance_q90 = 0.8 * 0.8;
    require(!methods::validate(measurements),
            "paper admission includes equality and needs a witness");
    measurements.paper_fraction = 1.0 / 16.0;
    require(methods::validate(measurements),
            "I01 paper admission retains an exact threshold-tie witness");
    measurements.luminance_q90 = 0;
    measurements.paper_fraction = 1.0 / 16.0;
    measurements.dark_fraction = 15.0 / 16.0;
    require(methods::validate(measurements),
            "source-zero rank groups can overlap without being summed");
}
inline void surface_white_phase_cases() {
    auto facts = completed_surface_facts();
    if (!facts.measurements) {
        require(false, "the completed I01 fixture owns measurements");
        return;
    }
    facts.measurements->luminance_q90 = 1;
    facts.measurements->paper_fraction = 1.0 / 4096.0;
    facts.measurements->dark_fraction = 1.0 / 16.0;
    facts.auto_predicates.paper_fraction = false;
    require(
        methods::validate(*facts.measurements),
        "sampled background quantiles do not admit every sampled background into the unit range");
    facts.disposition = methods::Disposition::skipped;
    facts.reason = methods::SurfaceReason::automatic_predicates;
    facts.counts.evaluated = 0;
    facts.counts.changed = 0;
    facts.gain_capped_samples = 0;
    facts.saturated_samples = 0;
    facts.max_gain = 1;
    const methods::SurfaceParameters automatic{.mode = methods::SurfaceMode::automatic};
    require(methods::validate(facts, automatic, surface_fact_extent),
            "I01 automatic skip retains weak white intersection without applying a unit-background "
            "contract");
    facts.disposition = methods::Disposition::applied;
    facts.reason = methods::SurfaceReason::none;
    facts.counts.evaluated = 4096;
    facts.counts.changed = 2000;
    facts.gain_capped_samples = 1000;
    facts.saturated_samples = 50;
    facts.max_gain = 2;
    require(!methods::validate(facts), "completed active I01 applies the stronger white-population "
                                       "bound after unit-background admission");
}
} // namespace docenhance::tests
