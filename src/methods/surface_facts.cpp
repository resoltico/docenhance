// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/surface_facts.hpp"

#include "docenhance/core/range.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameter_domains.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/surface.hpp"
#include "illumination_fact_checks.hpp"
#include "surface_detail.hpp"
#include "surface_fact_checks.hpp"

#include <algorithm>
#include <cmath>

namespace docenhance::methods {
IlluminationApplicationFacts surface_application(const SurfaceFacts& facts) noexcept {
    return {
        .counts = facts.counts,
        .gain_capped_samples = facts.gain_capped_samples,
        .saturated_samples = facts.saturated_samples,
        .min_gain = facts.min_gain,
        .max_gain = facts.max_gain,
    };
}
namespace {
bool no_predicates(const SurfacePredicates& predicates) noexcept {
    return std::ranges::all_of(predicates.values(), [](auto value) { return !value.has_value(); });
}
bool untouched(const SurfaceFacts& facts) noexcept {
    return facts.counts.evaluated == 0 && facts.counts.changed == 0 &&
           facts.gain_capped_samples == 0 && facts.saturated_samples == 0 && facts.min_gain == 1 &&
           facts.max_gain == 1;
}
bool no_grid(const SurfaceFacts& facts) noexcept {
    return !facts.resolved_cell && facts.cells == 0 && facts.measured_cells == 0 &&
           facts.dark_cells == 0 && !facts.background_reference && !facts.coverage &&
           !facts.solver && !facts.measurements && no_predicates(facts.auto_predicates);
}
bool grid_facts(const SurfaceFacts& facts) noexcept {
    if (!facts.resolved_cell) {
        return facts.cells == 0 && facts.measured_cells == 0 && facts.dark_cells == 0 &&
               !facts.background_reference && !facts.coverage;
    }
    if (!background_cell_domain.contains(*facts.resolved_cell) || facts.cells == 0 ||
        facts.cells > surface_cell_limit || facts.measured_cells > facts.cells ||
        facts.dark_cells > facts.cells - facts.measured_cells || !facts.coverage ||
        *facts.coverage != surface_coverage(facts.measured_cells, facts.cells)) {
        return false;
    }
    const auto initially_measured = facts.measured_cells + facts.dark_cells;
    if (initially_measured > facts.counts.eligible) {
        return false;
    }
    if (initially_measured == 0) {
        return !facts.background_reference;
    }
    const auto selected = image::nearest_rank_index(initially_measured, surface_reference_rank);
    return selected && facts.background_reference &&
           core::in_range(*facts.background_reference, std::exp(std::log(illumination_floor)),
                          1.0) &&
           facts.measured_cells >= initially_measured - *selected;
}
bool fitted(const SurfaceFacts& facts) noexcept {
    if (!facts.resolved_cell || !facts.coverage || !facts.solver || !facts.measurements ||
        facts.measured_cells == 0 || *facts.coverage < surface_explicit_coverage ||
        !validate(*facts.solver) || !validate(*facts.measurements) ||
        facts.measurements->count > facts.counts.eligible) {
        return false;
    }
    // Integer per-term upper bounds avoid pretending to reconstruct the unreported RHS norm.
    const auto log_bound = std::ceil(-std::log(illumination_floor));
    const auto norm_bound =
        log_bound * std::ceil(std::sqrt(static_cast<double>(facts.measured_cells)));
    if (facts.solver->tolerance < surface_relative_tolerance * surface_rhs_floor ||
        facts.solver->tolerance > surface_relative_tolerance * norm_bound ||
        ((facts.cells == 1 || facts.solver->iterations == 0) &&
         (facts.measurements->background_q10 != facts.measurements->background_q50 ||
          facts.measurements->background_q50 != facts.measurements->background_q90))) {
        return false;
    }
    return facts.auto_predicates == surface_decisions(*facts.measurements, *facts.coverage);
}
bool active(const SurfaceFacts& facts) noexcept {
    const auto total = facts.counts.eligible + facts.counts.protected_samples;
    if (!fitted(facts) || !facts.measurements ||
        facts.counts.eligible < std::min(surface_minimum_samples, total) ||
        facts.counts.evaluated != facts.counts.eligible ||
        (facts.counts.evaluated == 1 && facts.min_gain != facts.max_gain)) {
        return false;
    }
    const auto application = surface_application(facts);
    if (facts.measurements->luminance_q90 == 1) {
        const auto high = image::nearest_rank_index(facts.measurements->count, 0.9);
        const auto paper =
            surface_fraction_count(facts.measurements->paper_fraction, facts.measurements->count);
        const auto dark =
            surface_fraction_count(facts.measurements->dark_fraction, facts.measurements->count);
        if (!high || !paper || !dark || *paper < facts.measurements->count - *high ||
            *dark > *high) {
            return false;
        }
    }
    return illumination_effect_counts(application) &&
           illumination_source_counts(application, facts.measurements->count,
                                      facts.measurements->luminance_q90) &&
           (facts.measurements->target >
                std::max(facts.measurements->background_q90, illumination_floor) ||
            facts.min_gain == 1);
}
bool insufficient_cells(const SurfaceFacts& facts) noexcept {
    if (!facts.resolved_cell || !facts.coverage || *facts.coverage >= surface_automatic_coverage ||
        facts.solver || facts.measurements || !untouched(facts)) {
        return false;
    }
    const SurfacePredicates expected{.coverage = false};
    return facts.auto_predicates == expected;
}
} // namespace
bool validate(const SurfaceFacts& facts) noexcept {
    if (!validate(facts.counts) || !grid_facts(facts) ||
        !background_max_gain_domain.contains(facts.min_gain) ||
        !background_max_gain_domain.contains(facts.max_gain) || facts.min_gain > facts.max_gain ||
        facts.gain_capped_samples > facts.counts.evaluated ||
        facts.saturated_samples > facts.counts.evaluated) {
        return false;
    }
    const auto minimum =
        std::min(surface_minimum_samples, facts.counts.eligible + facts.counts.protected_samples);
    switch (facts.reason) {
    case SurfaceReason::no_eligible_samples:
        return facts.disposition == Disposition::no_change && facts.counts.eligible == 0 &&
               no_grid(facts) && untouched(facts);
    case SurfaceReason::zero_strength:
    case SurfaceReason::unit_gain:
        return facts.disposition == Disposition::no_change && facts.counts.eligible != 0 &&
               no_grid(facts) && untouched(facts);
    case SurfaceReason::insufficient_samples:
        return facts.disposition == Disposition::skipped && facts.counts.eligible != 0 &&
               facts.counts.eligible < minimum && no_grid(facts) && untouched(facts);
    case SurfaceReason::insufficient_cells:
        return facts.disposition == Disposition::skipped && facts.counts.eligible >= minimum &&
               insufficient_cells(facts);
    case SurfaceReason::automatic_predicates:
        return facts.disposition == Disposition::skipped && facts.counts.eligible >= minimum &&
               fitted(facts) && facts.coverage && *facts.coverage >= surface_automatic_coverage &&
               untouched(facts) &&
               std::ranges::any_of(facts.auto_predicates.values(),
                                   [](auto value) { return value.has_value() && !*value; });
    case SurfaceReason::none:
        return facts.disposition == Disposition::applied && facts.counts.changed != 0 &&
               active(facts);
    case SurfaceReason::no_effect:
        return facts.disposition == Disposition::no_change && facts.counts.changed == 0 &&
               active(facts);
    }
    return false;
}
} // namespace docenhance::methods
