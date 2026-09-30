// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "read_fields.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
namespace docenhance::bundle {
namespace {
bool unit(double value) {
    return std::isfinite(value) && value >= 0 && value <= 1;
}
bool measurements_agree(const methods::SurfaceMeasurements& m) {
    return m.stride != 0 && m.count != 0 && m.count <= methods::surface_sample_limit &&
           unit(m.target) && m.target > 0 && unit(m.background_q10) && unit(m.background_q50) &&
           unit(m.background_q90) && m.background_q10 <= m.background_q50 &&
           m.background_q50 <= m.background_q90 && unit(m.luminance_q90) &&
           std::isfinite(m.variation) && m.variation >= 0 && unit(m.paper_fraction) &&
           unit(m.dark_fraction);
}
bool disabled(const methods::IlluminationReport& r) {
    return r.status == methods::SurfaceStatus::disabled &&
           r.reason == methods::SurfaceReason::none && !r.cell && !r.solver && !r.measurements &&
           !r.background_reference && r.cells == 0 && r.evaluated_samples == 0 && r.min_gain == 1 &&
           r.max_gain == 1 &&
           std::ranges::all_of(r.predicates, [](const auto& value) { return !value; });
}
bool unmeasured(const methods::IlluminationReport& r) {
    return !r.cell && !r.solver && !r.measurements && r.cells == 0 && r.evaluated_samples == 0 &&
           r.changed_samples == 0 && r.min_gain == 1 && r.max_gain == 1;
}
bool fitted(const methods::IlluminationReport& r) {
    return r.solver && r.measurements && r.cell && r.evaluated_samples == r.eligible_samples;
}
bool reason_agrees(const methods::IlluminationReport& r) {
    if (!r.requested) {
        return disabled(r);
    }
    const auto& p = *r.requested;
    switch (r.status) {
    case methods::SurfaceStatus::applied:
        return r.reason == methods::SurfaceReason::none && r.solver && r.measurements && r.cell &&
               r.changed_samples != 0;
    case methods::SurfaceStatus::skipped:
        return p.mode == methods::SurfaceMode::automatic &&
               (r.reason == methods::SurfaceReason::insufficient_samples ||
                r.reason == methods::SurfaceReason::insufficient_cells ||
                r.reason == methods::SurfaceReason::automatic_predicates) &&
               r.evaluated_samples == 0;
    case methods::SurfaceStatus::no_change:
        switch (r.reason) {
        case methods::SurfaceReason::zero_strength:
            return p.strength == 0 && unmeasured(r);
        case methods::SurfaceReason::unit_gain:
            return p.max_gain == 1 && unmeasured(r);
        case methods::SurfaceReason::no_eligible_samples:
            return r.eligible_samples == 0 && unmeasured(r);
        case methods::SurfaceReason::no_effect:
            return fitted(r) && r.changed_samples == 0;
        default:
            return false;
        }
    case methods::SurfaceStatus::disabled:
    case methods::SurfaceStatus::failed:
        return false;
    }
    return false;
}
bool grid_agrees(const DeclaredBundle& d) {
    const auto& r = d.illumination;
    if (!r.cell) {
        return r.cells == 0;
    }
    if (!r.requested || *r.cell < methods::Surface::min_cell ||
        *r.cell > methods::Surface::max_cell) {
        return false;
    }
    const auto method = methods::Surface::create(*r.requested);
    if (!method) {
        return false;
    }
    const auto grid = methods::surface_grid(
        {.width = d.output.shape.width, .height = d.output.shape.height}, *method);
    return grid && grid->cell == *r.cell && std::uint64_t{grid->columns} * grid->rows == r.cells;
}
} // namespace
bool illumination_agrees(const DeclaredBundle& d) {
    const auto& r = d.illumination;
    if (r.measured_cells > r.cells || r.dark_cells > r.cells - r.measured_cells ||
        r.changed_samples > r.evaluated_samples || r.gain_capped_samples > r.evaluated_samples ||
        r.saturated_samples > r.evaluated_samples || r.evaluated_samples > r.eligible_samples ||
        r.min_gain < 1 || r.max_gain < r.min_gain || r.max_gain > methods::surface_gain_limit) {
        return false;
    }
    if (r.solver && (r.solver->residual < 0 || r.solver->tolerance <= 0 ||
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
    if (!d.protection && r.protected_samples != 0) {
        return false;
    }
    if (r.requested && r.max_gain > r.requested->max_gain) {
        return false;
    }
    return reason_agrees(r) && grid_agrees(d);
}
} // namespace docenhance::bundle
