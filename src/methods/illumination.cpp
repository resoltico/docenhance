// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/methods/illumination.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/methods/surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace docenhance::methods {
namespace {
bool in_range(double value, double low, double high) noexcept {
    return std::isfinite(value) && value >= low && value <= high;
}
} // namespace
core::Result<Surface> Surface::create(SurfaceParameters parameters) {
    constexpr double target_floor = 0.1;
    constexpr double quantile_floor = 0.75;
    constexpr double quantile_limit = 0.99;
    constexpr double smooth_floor = 0.1;
    constexpr double smooth_limit = 20;
    const bool valid_mode = parameters.mode == SurfaceMode::explicit_surface ||
                            parameters.mode == SurfaceMode::automatic;
    if (!valid_mode || !in_range(parameters.strength, 0, 1) ||
        !in_range(parameters.max_gain, 1, surface_gain_limit) ||
        !in_range(parameters.quantile, quantile_floor, quantile_limit) ||
        !in_range(parameters.smooth, smooth_floor, smooth_limit) ||
        (parameters.target && !in_range(*parameters.target, target_floor, 1)) ||
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
    return r.status == SurfaceStatus::disabled && r.reason == SurfaceReason::none && !r.cell &&
           !r.solver && !r.measurements && !r.background_reference && r.cells == 0 &&
           r.evaluated_samples == 0 && r.min_gain == 1 && r.max_gain == 1 &&
           std::ranges::all_of(r.predicates, [](const auto& value) { return !value; });
}
bool unmeasured(const IlluminationReport& r) {
    return !r.cell && !r.solver && !r.measurements && r.cells == 0 && r.evaluated_samples == 0 &&
           r.changed_samples == 0 && r.min_gain == 1 && r.max_gain == 1;
}
bool fitted(const IlluminationReport& r) {
    return r.solver && r.measurements && r.cell && r.evaluated_samples == r.eligible_samples;
}
bool reason_agrees(const IlluminationReport& r) {
    if (!r.requested) {
        return disabled(r);
    }
    const auto& p = *r.requested;
    switch (r.status) {
    case SurfaceStatus::applied:
        return r.reason == SurfaceReason::none && r.solver && r.measurements && r.cell &&
               r.changed_samples != 0;
    case SurfaceStatus::skipped:
        return p.mode == SurfaceMode::automatic &&
               (r.reason == SurfaceReason::insufficient_samples ||
                r.reason == SurfaceReason::insufficient_cells ||
                r.reason == SurfaceReason::automatic_predicates) &&
               r.evaluated_samples == 0;
    case SurfaceStatus::no_change:
        switch (r.reason) {
        case SurfaceReason::zero_strength:
            return p.strength == 0 && unmeasured(r);
        case SurfaceReason::unit_gain:
            return p.max_gain == 1 && unmeasured(r);
        case SurfaceReason::no_eligible_samples:
            return r.eligible_samples == 0 && unmeasured(r);
        case SurfaceReason::no_effect:
            return fitted(r) && r.changed_samples == 0;
        default:
            return false;
        }
    case SurfaceStatus::disabled:
    case SurfaceStatus::failed:
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
    const auto method = Surface::create(*r.requested);
    if (!method) {
        return false;
    }
    const auto grid = surface_grid(extent, *method);
    return grid && grid->cell == *r.cell && std::uint64_t{grid->columns} * grid->rows == r.cells;
}
} // namespace
bool valid_illumination(const IlluminationReport& report, image::Extent extent, bool protection) {
    const auto& r = report;
    if (r.measured_cells > r.cells || r.dark_cells > r.cells - r.measured_cells ||
        r.changed_samples > r.evaluated_samples || r.gain_capped_samples > r.evaluated_samples ||
        r.saturated_samples > r.evaluated_samples || r.evaluated_samples > r.eligible_samples ||
        !std::isfinite(r.min_gain) || !std::isfinite(r.max_gain) || r.min_gain < 1 ||
        r.max_gain < r.min_gain || r.max_gain > surface_gain_limit) {
        return false;
    }
    if (r.solver && (!std::isfinite(r.solver->residual) || !std::isfinite(r.solver->tolerance) ||
                     r.solver->residual < 0 || r.solver->tolerance <= 0 ||
                     r.solver->residual > r.solver->tolerance)) {
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
    if (!protection && r.protected_samples != 0) {
        return false;
    }
    if (r.requested && r.max_gain > r.requested->max_gain) {
        return false;
    }
    return reason_agrees(r) && grid_agrees(r, extent);
}
} // namespace docenhance::methods
