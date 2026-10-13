// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/illumination.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/morphology.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <variant>

namespace docenhance::methods {
namespace {
bool in_range(double value, double low, double high) noexcept {
    return std::isfinite(value) && value >= low && value <= high;
}
} // namespace
core::Result<Surface> Surface::create(SurfaceParameters parameters) {
    const bool valid_mode = parameters.mode == SurfaceMode::explicit_surface ||
                            parameters.mode == SurfaceMode::automatic;
    if (!valid_mode || !in_range(parameters.strength, 0, 1) ||
        !in_range(parameters.max_gain, 1, illumination_gain_limit) ||
        !in_range(parameters.quantile, surface_min_quantile, surface_max_quantile) ||
        !in_range(parameters.smooth, surface_min_smooth, surface_max_smooth) ||
        (parameters.target && !in_range(*parameters.target, illumination_min_target, 1)) ||
        (parameters.cell && (*parameters.cell < min_cell || *parameters.cell > max_cell))) {
        return core::failure(core::ErrorCode::argument, "Invalid I01 surface parameters");
    }
    return Surface{parameters};
}
namespace {
bool unit(double value) {
    return std::isfinite(value) && value >= 0 && value <= 1;
}
bool measurements_agree(const SurfaceMeasurements& m) {
    return m.stride != 0 && m.count != 0 && m.count <= surface_sample_limit && unit(m.target) &&
           m.target > 0 && unit(m.background_q10) && unit(m.background_q50) &&
           unit(m.background_q90) && m.background_q10 <= m.background_q50 &&
           m.background_q50 <= m.background_q90 && unit(m.luminance_q90) &&
           std::isfinite(m.variation) && m.variation >= 0 && unit(m.paper_fraction) &&
           unit(m.dark_fraction);
}
bool disabled(const IlluminationReport& r) {
    return r.status == IlluminationStatus::disabled && r.reason == IlluminationReason::none &&
           !r.cell && !r.solver && !r.measurements && !r.background_reference && r.cells == 0 &&
           r.evaluated_samples == 0 && r.min_gain == 1 && r.max_gain == 1 &&
           std::ranges::all_of(r.predicates, [](const auto& value) { return !value; });
}
bool unmeasured(const IlluminationReport& r) {
    return !r.cell && !r.solver && !r.measurements && r.cells == 0 && r.evaluated_samples == 0 &&
           r.changed_samples == 0 && r.min_gain == 1 && r.max_gain == 1 &&
           !r.background_reference &&
           std::ranges::all_of(r.predicates, [](const auto& value) { return !value; });
}
bool fitted_model(const IlluminationReport& r) {
    return r.solver && r.measurements && r.cell && r.background_reference && r.measured_cells != 0;
}
bool fitted(const IlluminationReport& r) {
    return fitted_model(r) && r.evaluated_samples == r.eligible_samples;
}
bool failed_surface(const IlluminationReport& report) {
    if (report.status != IlluminationStatus::failed) {
        return true;
    }
    if (report.reason == IlluminationReason::processing_failure) {
        return true;
    }
    if (!report.requested || report.progress != StageProgress::preparing) {
        return false;
    }
    const auto& parameters = std::get<SurfaceParameters>(*report.requested);
    const auto minimum =
        std::min(surface_minimum_samples, report.eligible_samples + report.protected_samples);
    if (parameters.mode != SurfaceMode::explicit_surface || parameters.strength == 0 ||
        parameters.max_gain == 1 || report.eligible_samples == 0) {
        return false;
    }
    if (report.reason == IlluminationReason::insufficient_samples) {
        return report.eligible_samples < minimum && unmeasured(report);
    }
    if (report.reason != IlluminationReason::insufficient_cells || !report.cell ||
        report.cells == 0) {
        return false;
    }
    const double coverage = static_cast<double>(report.measured_cells) / report.cells;
    const auto decision = report.predicates.front();
    return report.eligible_samples >= minimum && !report.solver && !report.measurements &&
           coverage < surface_explicit_coverage && decision && !*decision &&
           std::ranges::all_of(
               std::span{report.predicates}.subspan(1), [](const auto& value) { return !value; });
}
bool skipped(const IlluminationReport& r, const SurfaceParameters& parameters) {
    if (parameters.mode != SurfaceMode::automatic || r.evaluated_samples != 0) {
        return false;
    }
    switch (r.reason) {
    case IlluminationReason::automatic_predicates:
        return fitted_model(r) &&
               std::ranges::any_of(r.predicates, [](const auto& p) { return p && !*p; });
    case IlluminationReason::insufficient_samples: {
        return r.eligible_samples != 0 &&
               r.eligible_samples <
                   std::min(surface_minimum_samples, r.eligible_samples + r.protected_samples) &&
               unmeasured(r);
    }
    case IlluminationReason::insufficient_cells:
        return r.cell && r.cells != 0 && !r.solver && !r.measurements &&
               r.predicates.front() == false;
    default:
        return false;
    }
}
bool reason_agrees(const IlluminationReport& r) {
    if (!r.requested) {
        return disabled(r);
    }
    const auto& p = std::get<SurfaceParameters>(*r.requested);
    switch (r.status) {
    case IlluminationStatus::applied:
        return r.reason == IlluminationReason::none && fitted(r) && p.strength != 0 &&
               p.max_gain != 1 && r.changed_samples != 0 &&
               r.evaluated_samples == r.eligible_samples;
    case IlluminationStatus::skipped:
        return skipped(r, p);
    case IlluminationStatus::no_change:
        switch (r.reason) {
        case IlluminationReason::zero_strength:
            return r.eligible_samples != 0 && p.strength == 0 && unmeasured(r);
        case IlluminationReason::unit_gain:
            return r.eligible_samples != 0 && p.strength != 0 && p.max_gain == 1 && unmeasured(r);
        case IlluminationReason::no_eligible_samples:
            return r.eligible_samples == 0 && unmeasured(r);
        case IlluminationReason::no_effect:
            return p.strength != 0 && p.max_gain != 1 && fitted(r) && r.changed_samples == 0;
        default:
            return false;
        }
    case IlluminationStatus::unresolved:
    case IlluminationStatus::disabled:
    case IlluminationStatus::failed:
        return false;
    }
    return false;
}
bool grid_agrees(const IlluminationReport& r, image::Extent extent) {
    if (!r.cell) {
        return r.cells == 0;
    }
    if (!r.requested || *r.cell < Surface::min_cell || *r.cell > Surface::max_cell) {
        return false;
    }
    const auto method = Surface::create(std::get<SurfaceParameters>(*r.requested));
    if (!method) {
        return false;
    }
    const auto grid = surface_grid(extent, *method);
    return grid && grid->cell == *r.cell && std::uint64_t{grid->columns} * grid->rows == r.cells;
}
bool application_facts(const IlluminationReport& r) {
    return (r.evaluated_samples != 0 || (r.min_gain == 1 && r.max_gain == 1)) &&
           r.eligible_samples <= image::source_pixels_max &&
           r.protected_samples <= image::source_pixels_max - r.eligible_samples &&
           r.measured_cells <= r.cells && r.dark_cells <= r.cells - r.measured_cells &&
           r.changed_samples <= r.evaluated_samples &&
           r.gain_capped_samples <= r.evaluated_samples &&
           r.saturated_samples <= r.evaluated_samples &&
           r.evaluated_samples <= r.eligible_samples && std::isfinite(r.min_gain) &&
           std::isfinite(r.max_gain) && r.min_gain >= 1 && r.max_gain >= r.min_gain &&
           r.max_gain <= illumination_gain_limit;
}
bool surface_shape(const IlluminationReport& r) {
    if (r.requested && !Surface::create(std::get<SurfaceParameters>(*r.requested))) {
        return false;
    }
    if (r.cell && (*r.cell < Surface::min_cell || *r.cell > Surface::max_cell || r.cells == 0 ||
                   (r.requested && std::get<SurfaceParameters>(*r.requested).cell &&
                    r.cell != std::get<SurfaceParameters>(*r.requested).cell))) {
        return false;
    }
    return true;
}
bool fitted_facts(const IlluminationReport& report) {
    if (!report.measurements) {
        return true;
    }
    if (report.requested) {
        const auto& parameters = std::get<SurfaceParameters>(*report.requested);
        if (parameters.target && report.measurements->target != *parameters.target) {
            return false;
        }
    }
    auto expected = report;
    static_cast<void>(surface_eligible(expected));
    return expected.predicates == report.predicates;
}
bool surface_values(const IlluminationReport& r) {
    if (r.solver &&
        (!std::isfinite(r.solver->residual) || !std::isfinite(r.solver->tolerance) ||
         r.solver->residual < 0 || r.solver->tolerance < 0 ||
         (r.progress != StageProgress::preparing && r.solver->tolerance == 0) ||
         (r.progress != StageProgress::preparing && r.solver->residual > r.solver->tolerance))) {
        return false;
    }
    if (r.measurements &&
        (!measurements_agree(*r.measurements) || r.measurements->count > r.eligible_samples)) {
        return false;
    }
    if (r.background_reference &&
        (!unit(*r.background_reference) || *r.background_reference == 0)) {
        return false;
    }
    if (r.requested && r.max_gain > std::get<SurfaceParameters>(*r.requested).max_gain) {
        return false;
    }
    return true;
}
} // namespace
bool valid_illumination_observations(const IlluminationReport& report) {
    if (!application_facts(report) || !valid_progress(report)) {
        return false;
    }
    if (report.requested && std::holds_alternative<MorphologyParameters>(*report.requested)) {
        return valid_progress(report) && valid_morphology_observations(report);
    }
    if (report.morphology) {
        return false;
    }
    const auto& r = report;
    if (!surface_shape(r) || !surface_values(r) || !fitted_facts(r) || !failed_surface(r)) {
        return false;
    }
    if ((r.progress == StageProgress::prepared || r.progress == StageProgress::evaluating) &&
        !fitted_model(r)) {
        return false;
    }
    if (!valid_progress(r)) {
        return false;
    }
    if (!r.complete()) {
        return !r.requested || r.status == IlluminationStatus::failed ||
               (r.status == IlluminationStatus::unresolved &&
                (r.progress == StageProgress::preparing || (r.cell && r.solver && r.measurements)));
    }
    return reason_agrees(r);
}
bool valid_illumination(const IlluminationReport& r, image::Extent extent, bool protection) {
    if (!r.complete() || !valid_illumination_observations(r) ||
        (!protection && r.protected_samples != 0)) {
        return false;
    }
    if (r.requested && std::holds_alternative<MorphologyParameters>(*r.requested)) {
        return valid_morphology_report(r, extent, protection);
    }
    return grid_agrees(r, extent);
}
} // namespace docenhance::methods
