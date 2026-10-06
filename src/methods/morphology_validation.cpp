// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/limits.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"
#include "morphology_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <variant>
namespace docenhance::methods {
namespace {
bool unit(double value) noexcept {
    return std::isfinite(value) && value >= 0 && value <= 1;
}
bool untouched(const IlluminationReport& r, const MorphologyMeasurements& m) {
    return !m.analysis_fill && !m.target && m.count == 0 && m.stride == 0 && !m.fallback &&
           m.field_bytes == 0 && m.preparation_charge_peak == 0 && m.background_q10 == 0 &&
           m.background_q50 == 0 && m.background_q90 == 0 && m.background_min == 0 &&
           m.background_max == 0 && r.evaluated_samples == 0 && r.changed_samples == 0 &&
           r.gain_capped_samples == 0 && r.saturated_samples == 0 && r.min_gain == 1 &&
           r.max_gain == 1;
}
bool prepared(const MorphologyMeasurements& m, MorphologyParameters p, image::Extent extent,
              std::uint64_t eligible) {
    const auto maximum = std::max(extent.width, extent.height);
    const auto stride =
        std::max(1U, (maximum / morphology_lattice_extent) +
                         static_cast<std::uint32_t>(maximum % morphology_lattice_extent != 0));
    const auto shape = image::plane_shape(extent.width, extent.height, sizeof(double));
    if (!shape) {
        return false;
    }
    const auto bytes = image::plane_bytes(*shape);
    return bytes && m.field_bytes == *bytes && m.preparation_charge_peak >= 2 * m.field_bytes &&
           m.preparation_charge_peak <= core::continuous_processing_budget && m.stride == stride &&
           m.count != 0 && m.count <= eligible && m.count <= illumination_sample_limit &&
           (!m.fallback || m.count == 1) && m.analysis_fill && unit(*m.analysis_fill) && m.target &&
           unit(*m.target) && *m.target == p.target.value_or(m.background_q90) &&
           unit(m.background_min) && unit(m.background_max) &&
           m.background_min <= m.background_q10 && m.background_q10 <= m.background_q50 &&
           m.background_q50 <= m.background_q90 && m.background_q90 <= m.background_max;
}
} // namespace
bool valid_morphology_observations(const IlluminationReport& r) {
    if (!r.requested || !std::holds_alternative<MorphologyParameters>(*r.requested) || r.cell ||
        r.cells != 0 || r.measured_cells != 0 || r.dark_cells != 0 || r.solver || r.measurements ||
        r.background_reference ||
        !std::ranges::all_of(r.predicates, [](const auto& p) { return !p; }) ||
        r.status == IlluminationStatus::disabled || r.status == IlluminationStatus::skipped ||
        r.reason == IlluminationReason::insufficient_samples ||
        r.reason == IlluminationReason::insufficient_cells ||
        r.reason == IlluminationReason::automatic_predicates) {
        return false;
    }
    const auto p = std::get<MorphologyParameters>(*r.requested);
    if (!Morphology::create(p) || r.max_gain > p.max_gain) {
        return false;
    }
    if (!r.morphology) {
        return !r.complete;
    }
    const auto& m = *r.morphology;
    constexpr std::uint32_t support_sigmas = 3;
    constexpr auto maximum_stride =
        (image::source_pixels_max + morphology_lattice_extent - 1) / morphology_lattice_extent;
    const bool radius =
        p.radius ? m.radius == *p.radius
                 : m.radius >= morphology_automatic_min && m.radius <= morphology_automatic_max;
    return radius && m.radius >= Morphology::min_radius && m.radius <= Morphology::max_radius &&
           m.sigma == static_cast<double>(m.radius) / 2 &&
           m.gaussian_radius == ((support_sigmas * m.radius) + 1) / 2 &&
           m.count <= illumination_sample_limit && m.count <= r.eligible_samples &&
           m.stride <= maximum_stride && ((m.count == 0) == (m.stride == 0)) &&
           (!m.fallback || m.count == 1) && (!m.analysis_fill || unit(*m.analysis_fill)) &&
           (!m.target || unit(*m.target)) && unit(m.background_q10) && unit(m.background_q50) &&
           unit(m.background_q90) && unit(m.background_min) && unit(m.background_max) &&
           m.field_bytes <= core::continuous_processing_budget &&
           m.preparation_charge_peak >= m.field_bytes &&
           m.preparation_charge_peak <= core::continuous_processing_budget;
}
bool valid_morphology_report(const IlluminationReport& r, image::Extent extent, bool protection) {
    if (!r.complete || !r.requested || !r.morphology || !valid_morphology_observations(r)) {
        return false;
    }
    const auto p = std::get<MorphologyParameters>(*r.requested);
    const auto method = Morphology::create(p);
    const auto total = std::uint64_t{extent.width} * extent.height;
    if (!method || extent.width == 0 || extent.height == 0 || total > image::source_pixels_max ||
        r.protected_samples > total || r.eligible_samples != total - r.protected_samples ||
        (!protection && r.protected_samples != 0) || r.changed_samples > r.evaluated_samples ||
        r.gain_capped_samples > r.evaluated_samples || r.saturated_samples > r.evaluated_samples ||
        r.evaluated_samples > r.eligible_samples || !std::isfinite(r.min_gain) ||
        !std::isfinite(r.max_gain) || r.min_gain < 1 || r.max_gain < r.min_gain ||
        r.max_gain > p.max_gain) {
        return false;
    }
    const auto& m = *r.morphology;
    constexpr std::uint32_t gaussian_support_sigmas = 3;
    if (m.radius != morphology_radius(extent, *method) ||
        m.sigma != static_cast<double>(m.radius) / 2 ||
        m.gaussian_radius != ((gaussian_support_sigmas * m.radius) + 1) / 2) {
        return false;
    }
    const bool algebraic = p.strength == 0 || p.max_gain == 1 || r.eligible_samples == 0;
    if (algebraic) {
        auto reason = IlluminationReason::no_eligible_samples;
        if (p.max_gain == 1) {
            reason = IlluminationReason::unit_gain;
        }
        if (p.strength == 0) {
            reason = IlluminationReason::zero_strength;
        }
        return r.status == IlluminationStatus::no_change && r.reason == reason && untouched(r, m);
    }
    constexpr std::uint64_t minimum_samples = 16;
    if (r.eligible_samples < std::min(minimum_samples, total) ||
        r.evaluated_samples != r.eligible_samples || !prepared(m, p, extent, r.eligible_samples)) {
        return false;
    }
    return (r.status == IlluminationStatus::applied && r.reason == IlluminationReason::none &&
            r.changed_samples != 0) ||
           (r.status == IlluminationStatus::no_change &&
            r.reason == IlluminationReason::no_effect && r.changed_samples == 0);
}
} // namespace docenhance::methods
