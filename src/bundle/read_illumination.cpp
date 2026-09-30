// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/bundle/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/illumination.hpp"
#include "read_fields.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
namespace docenhance::bundle {
namespace {
core::Result<methods::SurfaceParameters> parameters(const RecordJson& value) {
    const auto& target = record_field(value, "target");
    const auto& cell = record_field(value, "cell");
    methods::SurfaceParameters const p{
        .mode = record_text(record_field(value, "mode")) == "auto"
                    ? methods::SurfaceMode::automatic
                    : methods::SurfaceMode::explicit_surface,
        .strength = record_number(record_field(value, "strength")),
        .max_gain = record_number(record_field(value, "max_gain")),
        .target = target.is_string() ? std::nullopt : std::optional{record_number(target)},
        .cell = cell.is_string()
                    ? std::nullopt
                    : std::optional{static_cast<std::uint32_t>(record_integer(cell, UINT32_MAX))},
        .quantile = record_number(record_field(value, "quantile")),
        .smooth = record_number(record_field(value, "smooth")),
    };
    auto admitted = methods::Surface::create(p);
    if (!admitted) {
        return std::unexpected(admitted.error());
    }
    return admitted->parameters();
}
methods::SurfaceMeasurements measurements(const RecordJson& v) {
    return {
        .stride = static_cast<std::uint32_t>(record_integer(record_field(v, "stride"), UINT32_MAX)),
        .count = static_cast<std::uint32_t>(record_integer(record_field(v, "count"), UINT32_MAX)),
        .fallback = record_boolean(record_field(v, "fallback")),
        .target = record_number(record_field(v, "target")),
        .background_q10 = record_number(record_field(v, "background_q10")),
        .background_q50 = record_number(record_field(v, "background_q50")),
        .background_q90 = record_number(record_field(v, "background_q90")),
        .luminance_q90 = record_number(record_field(v, "luminance_q90")),
        .variation = record_number(record_field(v, "variation")),
        .paper_fraction = record_number(record_field(v, "paper_fraction")),
        .dark_fraction = record_number(record_field(v, "dark_fraction")),
    };
}
void observations(const RecordJson& v, methods::IlluminationReport& r) {
    r.eligible_samples = record_integer(record_field(v, "eligible_samples"), UINT64_MAX);
    r.protected_samples = record_integer(record_field(v, "protected_samples"), UINT64_MAX);
    r.cells = static_cast<std::uint32_t>(record_integer(record_field(v, "cells"), UINT32_MAX));
    r.measured_cells =
        static_cast<std::uint32_t>(record_integer(record_field(v, "measured_cells"), UINT32_MAX));
    r.dark_cells =
        static_cast<std::uint32_t>(record_integer(record_field(v, "dark_cells"), UINT32_MAX));
    const auto& cell = record_field(v, "resolved_cell");
    if (!cell.is_null()) {
        r.cell = static_cast<std::uint32_t>(record_integer(cell, UINT32_MAX));
    }
    const auto& reference = record_field(v, "background_reference");
    if (!reference.is_null()) {
        r.background_reference = record_number(reference);
    }
    const auto& m = record_field(v, "measurements");
    if (!m.is_null()) {
        r.measurements = measurements(m);
    }
    const auto& s = record_field(v, "solver");
    if (!s.is_null()) {
        r.solver = methods::SolverReport{
            .iterations =
                static_cast<unsigned>(record_integer(record_field(s, "iterations"), UINT32_MAX)),
            .residual = record_number(record_field(s, "residual")),
            .tolerance = record_number(record_field(s, "tolerance")),
        };
    }
}
void application(const RecordJson& v, methods::IlluminationReport& r) {
    const auto& a = record_field(v, "application");
    r.evaluated_samples = record_integer(record_field(a, "evaluated_samples"), UINT64_MAX);
    r.changed_samples = record_integer(record_field(a, "changed_samples"), UINT64_MAX);
    r.gain_capped_samples = record_integer(record_field(a, "gain_capped_samples"), UINT64_MAX);
    r.saturated_samples = record_integer(record_field(a, "saturated_samples"), UINT64_MAX);
    r.min_gain = record_number(record_field(a, "min_gain"));
    r.max_gain = record_number(record_field(a, "max_gain"));
    constexpr auto names = std::to_array({
        "coverage",
        "bright_quantile",
        "median_background",
        "background_variation",
        "paper_fraction",
        "dark_fraction",
    });
    const auto& predicates = record_field(v, "auto_predicates");
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto& p = record_field(predicates, names.at(i));
        if (!p.is_null()) {
            r.predicates.at(i) = record_boolean(p);
        }
    }
}
} // namespace
core::Result<methods::IlluminationReport> record_illumination(const RecordJson& value) {
    methods::IlluminationReport r;
    constexpr auto statuses = std::to_array({
        methods::SurfaceStatus::disabled,
        methods::SurfaceStatus::no_change,
        methods::SurfaceStatus::skipped,
        methods::SurfaceStatus::applied,
        methods::SurfaceStatus::failed,
    });
    constexpr auto reasons = std::to_array({
        methods::SurfaceReason::none,
        methods::SurfaceReason::zero_strength,
        methods::SurfaceReason::unit_gain,
        methods::SurfaceReason::no_eligible_samples,
        methods::SurfaceReason::insufficient_samples,
        methods::SurfaceReason::insufficient_cells,
        methods::SurfaceReason::automatic_predicates,
        methods::SurfaceReason::no_effect,
        methods::SurfaceReason::processing_failure,
    });
    for (const auto status : statuses) {
        if (status_name(status) == record_text(record_field(value, "status"))) {
            r.status = status;
        }
    }
    for (const auto reason : reasons) {
        if (reason_name(reason) == record_text(record_field(value, "reason"))) {
            r.reason = reason;
        }
    }
    r.complete = record_boolean(record_field(value, "complete"));
    const auto& requested = record_field(value, "requested");
    if (!requested.is_null()) {
        auto p = parameters(requested);
        if (!p) {
            return std::unexpected(p.error());
        }
        r.requested = *p;
    }
    observations(value, r);
    application(value, r);
    return r;
}
} // namespace docenhance::bundle
