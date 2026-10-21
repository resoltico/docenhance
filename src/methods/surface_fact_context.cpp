// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/surface.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "illumination_fact_checks.hpp"
#include "illumination_gain.hpp"
#include "surface_detail.hpp"
#include "surface_fact_checks.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace docenhance::methods {
namespace {
bool sampling_agrees(const SurfaceFacts& facts, image::Extent extent) noexcept {
    if (!facts.measurements) {
        return false;
    }
    const auto& measurements = *facts.measurements;
    const auto stride = surface_lattice_stride(extent);
    const auto columns = std::uint64_t{1} + ((extent.width - 1) / stride);
    const auto rows = std::uint64_t{1} + ((extent.height - 1) / stride);
    const auto lattice = columns * rows;
    if (measurements.stride != stride) {
        return false;
    }
    if (measurements.fallback) {
        return stride != 1 && facts.counts.protected_samples >= lattice &&
               measurements.count ==
                   std::min(facts.counts.eligible, std::uint64_t{surface_sample_limit});
    }
    return measurements.count <= lattice &&
           (lattice <= facts.counts.protected_samples ||
            measurements.count >= lattice - facts.counts.protected_samples);
}
bool active_context(const SurfaceFacts& facts, SurfaceParameters parameters) noexcept {
    if (!facts.measurements || facts.max_gain > parameters.max_gain ||
        ((facts.max_gain == parameters.max_gain) != (facts.gain_capped_samples != 0)) ||
        ((facts.min_gain == parameters.max_gain) !=
         (facts.gain_capped_samples == facts.counts.evaluated))) {
        return false;
    }
    const auto& measurements = *facts.measurements;
    if (!facts.solver) {
        return false;
    }
    if (facts.cells == 1 || facts.solver->iterations == 0) {
        const auto gain =
            std::clamp(illumination_gain_ratio(measurements.background_q90, measurements.target),
                       1.0, parameters.max_gain);
        if (facts.min_gain != gain || facts.max_gain != gain) {
            return false;
        }
    }
    return illumination_quantile_gains(surface_application(facts), measurements.count,
                                       std::array{
                                           measurements.background_q10,
                                           measurements.background_q50,
                                           measurements.background_q90,
                                       },
                                       measurements.target, parameters.max_gain);
}
} // namespace
bool validate(const SurfaceFacts& facts, SurfaceParameters parameters,
              image::Extent extent) noexcept {
    if (!validate(facts) || !valid_parameters(parameters) || extent.width == 0 ||
        extent.height == 0 ||
        !counts_match(facts.counts, std::uint64_t{extent.width} * extent.height)) {
        return false;
    }
    if (facts.counts.eligible == 0) {
        return facts.reason == SurfaceReason::no_eligible_samples;
    }
    if (parameters.strength == 0) {
        return facts.reason == SurfaceReason::zero_strength;
    }
    if (parameters.max_gain == 1) {
        return facts.reason == SurfaceReason::unit_gain;
    }
    const auto minimum =
        std::min(surface_minimum_samples, std::uint64_t{extent.width} * extent.height);
    if (facts.counts.eligible < minimum) {
        return parameters.mode == SurfaceMode::automatic &&
               facts.reason == SurfaceReason::insufficient_samples;
    }
    const auto method = Surface::create(parameters);
    if (!method) {
        return false;
    }
    const auto grid = surface_grid_shape(extent, *method);
    const auto cells = std::uint64_t{grid.columns} * grid.rows;
    if (cells > surface_cell_limit || !facts.resolved_cell || *facts.resolved_cell != grid.cell ||
        facts.cells != cells || !facts.coverage) {
        return false;
    }
    if (facts.counts.eligible <
        surface_minimum_support(grid, facts.measured_cells + facts.dark_cells)) {
        return false;
    }
    if (facts.reason == SurfaceReason::insufficient_cells) {
        return parameters.mode == SurfaceMode::automatic;
    }
    const auto required = parameters.mode == SurfaceMode::automatic ? surface_automatic_coverage
                                                                    : surface_explicit_coverage;
    if (*facts.coverage < required || !facts.measurements || !sampling_agrees(facts, extent) ||
        facts.measurements->target !=
            parameters.target.value_or(facts.measurements->background_q90)) {
        return false;
    }
    const bool passed = std::ranges::all_of(facts.auto_predicates.values(),
                                            [](auto value) { return value.value_or(false); });
    if (parameters.mode == SurfaceMode::automatic && !passed) {
        return facts.reason == SurfaceReason::automatic_predicates;
    }
    return (facts.reason == SurfaceReason::none || facts.reason == SurfaceReason::no_effect) &&
           active_context(facts, parameters);
}
} // namespace docenhance::methods
