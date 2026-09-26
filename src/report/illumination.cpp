// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "illumination.hpp"

#include "docenhance/methods/illumination.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
namespace docenhance::report {
namespace {
using Json = nlohmann::ordered_json;
std::string_view status_name(methods::SurfaceStatus status) noexcept {
    switch (status) {
    case methods::SurfaceStatus::disabled:
        return "disabled";
    case methods::SurfaceStatus::no_change:
        return "no_change";
    case methods::SurfaceStatus::skipped:
        return "skipped";
    case methods::SurfaceStatus::applied:
        return "applied";
    case methods::SurfaceStatus::failed:
        return "failed";
    }
    return "failed";
}
std::string_view reason_name(methods::SurfaceReason reason) noexcept {
    switch (reason) {
    case methods::SurfaceReason::none:
        return "none";
    case methods::SurfaceReason::zero_strength:
        return "zero_strength";
    case methods::SurfaceReason::unit_gain:
        return "unit_gain";
    case methods::SurfaceReason::no_eligible_samples:
        return "no_eligible_samples";
    case methods::SurfaceReason::insufficient_samples:
        return "insufficient_samples";
    case methods::SurfaceReason::insufficient_cells:
        return "insufficient_cells";
    case methods::SurfaceReason::automatic_predicates:
        return "automatic_predicates";
    case methods::SurfaceReason::no_effect:
        return "no_effect";
    case methods::SurfaceReason::processing_failure:
        return "processing_failure";
    }
    return "processing_failure";
}
Json parameters(const methods::SurfaceParameters& p) {
    return {
        {"mode", p.mode == methods::SurfaceMode::automatic ? "auto" : "surface"},
        {"strength", p.strength},
        {"max_gain", p.max_gain},
        {"target", p.target ? Json(*p.target) : Json("source")},
        {"cell", p.cell ? Json(*p.cell) : Json("auto")},
        {"quantile", p.quantile},
        {"smooth", p.smooth},
    };
}
Json measurements(const methods::SurfaceMeasurements& p) {
    return {
        {"stride", p.stride},
        {"count", p.count},
        {"fallback", p.fallback},
        {"target", p.target},
        {"background_q10", p.background_q10},
        {"background_q50", p.background_q50},
        {"background_q90", p.background_q90},
        {"luminance_q90", p.luminance_q90},
        {"variation", p.variation},
        {"paper_fraction", p.paper_fraction},
        {"dark_fraction", p.dark_fraction},
    };
}
Json predicates(const methods::IlluminationReport& r) {
    constexpr auto names = std::to_array<std::string_view>({
        "coverage",
        "bright_quantile",
        "median_background",
        "background_variation",
        "paper_fraction",
        "dark_fraction",
    });
    Json result = Json::object();
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto& predicate = r.predicates.at(i);
        result.emplace(names.at(i), predicate ? Json(*predicate) : Json(nullptr));
    }
    return result;
}
Json fraction(std::uint64_t count, std::uint64_t total) {
    return total == 0 ? Json(nullptr)
                      : Json(static_cast<double>(count) / static_cast<double>(total));
}
} // namespace
nlohmann::ordered_json illumination_fields(const methods::IlluminationReport& r) {
    Json identity = nullptr;
    if (r.requested) {
        constexpr auto method = methods::Surface::descriptor();
        identity = {{"id", method.id}, {"method_version", method.method_version}};
    }
    Json solver = nullptr;
    if (r.solver) {
        solver = {
            {"iterations", r.solver->iterations},
            {"residual", r.solver->residual},
            {"tolerance", r.solver->tolerance},
        };
    }
    return {
        {"status", status_name(r.status)},
        {"complete", r.complete},
        {"reason", reason_name(r.reason)},
        {"method", identity},
        {"requested", r.requested ? parameters(*r.requested) : Json(nullptr)},
        {"resolved_cell", r.cell ? Json(*r.cell) : Json(nullptr)},
        {"eligible_samples", r.eligible_samples},
        {"protected_samples", r.protected_samples},
        {"cells", r.cells},
        {"measured_cells", r.measured_cells},
        {"dark_cells", r.dark_cells},
        {
            "background_reference",
            r.background_reference ? Json(*r.background_reference) : Json(nullptr),
        },
        {"coverage", fraction(r.measured_cells, r.cells)},
        {"solver", solver},
        {"measurements", r.measurements ? measurements(*r.measurements) : Json(nullptr)},
        {"auto_predicates", predicates(r)},
        {
            "application",
            {
                {"evaluated_samples", r.evaluated_samples},
                {"changed_samples", r.changed_samples},
                {"gain_capped_samples", r.gain_capped_samples},
                {"saturated_samples", r.saturated_samples},
                {"min_gain", r.min_gain},
                {"max_gain", r.max_gain},
                {"gain_capped_fraction", fraction(r.gain_capped_samples, r.evaluated_samples)},
                {"saturated_fraction", fraction(r.saturated_samples, r.evaluated_samples)},
            },
        },
    };
}
std::string illumination_text(const methods::IlluminationReport& r) {
    std::string text = "Illumination: " + std::string(status_name(r.status)) + " (" +
                       std::string(reason_name(r.reason)) +
                       "); protected=" + std::to_string(r.protected_samples) + "\n";
    if (r.solver) {
        text += "I01 solver: iterations=" + std::to_string(r.solver->iterations) +
                "; residual=" + std::to_string(r.solver->residual) + "\n";
    }
    return text;
}
} // namespace docenhance::report
