// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/surface_facts.hpp"

#include <algorithm>
#include <cmath>

namespace docenhance::tests {
inline constexpr image::Extent surface_fact_extent{.width = 64, .height = 64};
inline constexpr methods::SurfaceParameters surface_fact_parameters{};
inline methods::SurfaceFacts completed_surface_facts() {
    return {
        .disposition = methods::Disposition::applied,
        .reason = methods::SurfaceReason::none,
        .counts = {.eligible = 4096, .evaluated = 4096, .changed = 2000},
        .resolved_cell = 16,
        .cells = 16,
        .measured_cells = 16,
        .background_reference = 0.9,
        .coverage = 1,
        .solver = methods::SolverReport{.iterations = 8, .residual = 1e-9, .tolerance = 1e-7},
        .measurements =
            methods::SurfaceMeasurements{
                .stride = 1,
                .count = 4096,
                .target = 0.8,
                .background_q10 = 0.3,
                .background_q50 = 0.5,
                .background_q90 = 0.8,
                .luminance_q90 = 0.7,
                .variation = (0.8 - 0.3) / 0.8,
                .paper_fraction = 0.75,
                .dark_fraction = 0.0625,
            },
        .auto_predicates =
            methods::SurfacePredicates{
                .coverage = true,
                .bright_quantile = true,
                .median_background = true,
                .background_variation = true,
                .paper_fraction = true,
                .dark_fraction = true,
            },
        .gain_capped_samples = 1000,
        .saturated_samples = 50,
        .min_gain = 1,
        .max_gain = 2,
    };
}
inline methods::SurfaceFacts singleton_surface_facts() {
    return {
        .reason = methods::SurfaceReason::no_effect,
        .counts = {.eligible = 1, .evaluated = 1},
        .resolved_cell = 16,
        .cells = 1,
        .measured_cells = 1,
        .background_reference = 0.10000000000000002,
        .coverage = 1,
        .solver = methods::SolverReport{.tolerance = 2.3025850929940455e-8},
        .measurements =
            methods::SurfaceMeasurements{
                .stride = 1,
                .count = 1,
                .target = 0.10000000000000002,
                .background_q10 = 0.10000000000000002,
                .background_q50 = 0.10000000000000002,
                .background_q90 = 0.10000000000000002,
                .luminance_q90 = 0.1,
                .paper_fraction = 1,
            },
        .auto_predicates =
            methods::SurfacePredicates{
                .coverage = true,
                .bright_quantile = false,
                .median_background = false,
                .background_variation = false,
                .paper_fraction = true,
                .dark_fraction = false,
            },
    };
}
inline methods::SurfaceFacts constant_initial_surface_facts() {
    constexpr double source = 0.1;
    constexpr double first_weight = 49.0 / 64.0;
    constexpr double relative = 1e-8;
    constexpr double rhs_floor = 1e-12;
    const auto logarithm = std::log(source);
    const auto first_rhs = first_weight * logarithm;
    const auto mean = (first_rhs + logarithm) / (first_weight + 1);
    const auto background = std::exp(mean);
    const auto first_residual = first_rhs - (first_weight * mean);
    const auto second_residual = logarithm - mean;
    auto facts = singleton_surface_facts();
    facts.counts = {.eligible = 57, .protected_samples = 15, .evaluated = 57};
    facts.resolved_cell = 8;
    facts.cells = 2;
    facts.measured_cells = 2;
    facts.background_reference = std::exp(logarithm);
    facts.solver = methods::SolverReport{
        .iterations = 0,
        .residual =
            std::sqrt((first_residual * first_residual) + (second_residual * second_residual)),
        .tolerance =
            relative *
            std::max(std::sqrt((first_rhs * first_rhs) + (logarithm * logarithm)), rhs_floor),
    };
    facts.measurements = methods::SurfaceMeasurements{
        .stride = 1,
        .count = 57,
        .target = background,
        .background_q10 = background,
        .background_q50 = background,
        .background_q90 = background,
        .luminance_q90 = source,
        .paper_fraction = 1,
    };
    return facts;
}
} // namespace docenhance::tests
