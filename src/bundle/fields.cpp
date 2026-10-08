// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"

#include "docenhance/bundle/record.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/otsu.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>
namespace docenhance::bundle {
nlohmann::ordered_json
binarization_fields(const std::optional<methods::OtsuObservation>& observation) {
    if (!observation) {
        return nullptr;
    }
    return {
        {"threshold_bin", observation->threshold_bin},
        {"single_bin_fallback", observation->single_bin_fallback},
    };
}
using Json = nlohmann::ordered_json;
std::string_view status_name(methods::IlluminationStatus status) noexcept {
    switch (status) {
    case methods::IlluminationStatus::disabled:
        return "disabled";
    case methods::IlluminationStatus::no_change:
        return "no_change";
    case methods::IlluminationStatus::skipped:
        return "skipped";
    case methods::IlluminationStatus::applied:
        return "applied";
    case methods::IlluminationStatus::failed:
        return "failed";
    }
    return "failed";
}
std::string_view reason_name(methods::IlluminationReason reason) noexcept {
    switch (reason) {
    case methods::IlluminationReason::none:
        return "none";
    case methods::IlluminationReason::zero_strength:
        return "zero_strength";
    case methods::IlluminationReason::unit_gain:
        return "unit_gain";
    case methods::IlluminationReason::no_eligible_samples:
        return "no_eligible_samples";
    case methods::IlluminationReason::insufficient_samples:
        return "insufficient_samples";
    case methods::IlluminationReason::insufficient_cells:
        return "insufficient_cells";
    case methods::IlluminationReason::automatic_predicates:
        return "automatic_predicates";
    case methods::IlluminationReason::no_effect:
        return "no_effect";
    case methods::IlluminationReason::processing_failure:
        return "processing_failure";
    }
    return "processing_failure";
}
namespace {
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
        const auto method = std::holds_alternative<methods::MorphologyParameters>(*r.requested)
                                ? methods::Morphology::descriptor()
                                : methods::Surface::descriptor();
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
    Json requested = nullptr;
    if (r.requested) {
        requested = std::visit(
            [](const auto& p) {
                using Parameters = std::decay_t<decltype(p)>;
                if constexpr (std::is_same_v<Parameters, methods::SurfaceParameters>) {
                    return parameters(p);
                } else {
                    return morphology_parameters_fields(p);
                }
            },
            *r.requested);
    }
    return {
        {"status", status_name(r.status)},
        {"complete", r.complete},
        {"reason", reason_name(r.reason)},
        {"method", identity},
        {"requested", requested},
        {"morphology", r.morphology ? morphology_fields(*r.morphology) : Json(nullptr)},
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
std::vector<std::string_view> conversion_warnings(const image::ConversionReport& report) {
    std::vector<std::string_view> codes;
    if (report.assumed_transfer || report.assumed_primaries) {
        codes.emplace_back("W_PROFILE_ASSUMED");
    }
    if (report.interpretation == image::Interpretation::overridden_srgb) {
        codes.emplace_back("W_PROFILE_OVERRIDDEN");
    }
    if (report.flattened_pixels != 0) {
        codes.emplace_back("W_ALPHA_FLATTENED");
    }
    if (report.depth_reduced) {
        codes.emplace_back("W_DEPTH_REDUCED");
    }
    return codes;
}
namespace {
Json dimensions(image::RasterShape shape) {
    return {
        {"width", shape.width},
        {"height", shape.height},
        {"channels", image::components(shape.model)},
        {"bit_depth", shape.depth.bits()},
    };
}
Json warnings(const image::ConversionReport& value) {
    Json result = Json::array();
    for (const auto code : conversion_warnings(value)) {
        result.push_back(code);
    }
    return result;
}
} // namespace
nlohmann::ordered_json record_fields(const std::string& run, const core::ContentIdentity& record) {
    return {
        {"run", run},
        {"path", record_name},
        {"sha256", record.sha256},
        {"bytes", record.bytes},
    };
}
nlohmann::ordered_json conversion_fields(const image::ConversionReport& report) {
    Json resolution = nullptr;
    if (report.resolution) {
        resolution = {{"x_ppm", report.resolution->x}, {"y_ppm", report.resolution->y}};
    }
    return {
        {"decoded_input", dimensions(report.source)},
        {"encoded_output", dimensions(report.output)},
        {"profile_decision", image::interpretation_name(report.interpretation)},
        {"assumed_transfer", report.assumed_transfer},
        {"assumed_primaries", report.assumed_primaries},
        {"source_orientation", report.orientation.code()},
        {"resolution", resolution},
        {"alpha_flattened_pixels", report.flattened_pixels},
        {"clipped_components", report.clipped_components},
        {"depth_reduced", report.depth_reduced},
        {"verified", report.verified},
        {"warnings", warnings(report)},
    };
}
} // namespace docenhance::bundle
