// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"
#include "docenhance/methods/illumination.hpp"

#include <nlohmann/json.hpp>
namespace docenhance::bundle {
nlohmann::ordered_json morphology_parameters_fields(const methods::MorphologyParameters& p) {
    using Json = nlohmann::ordered_json;
    return {
        {"mode", "morph"},
        {"strength", p.strength},
        {"max_gain", p.max_gain},
        {"target", p.target ? Json(*p.target) : Json("source")},
        {"radius", p.radius ? Json(*p.radius) : Json("auto")},
    };
}
nlohmann::ordered_json morphology_fields(const methods::MorphologyMeasurements& m) {
    using Json = nlohmann::ordered_json;
    return {
        {"radius", m.radius},
        {"sigma", m.sigma},
        {"gaussian_radius", m.gaussian_radius},
        {"stride", m.stride},
        {"count", m.count},
        {"fallback", m.fallback},
        {"analysis_fill", m.analysis_fill ? Json(*m.analysis_fill) : Json(nullptr)},
        {"target", m.target ? Json(*m.target) : Json(nullptr)},
        {"background_q10", m.background_q10},
        {"background_q50", m.background_q50},
        {"background_q90", m.background_q90},
        {"background_min", m.background_min},
        {"background_max", m.background_max},
        {"field_bytes", m.field_bytes},
        {"preparation_charge_peak", m.preparation_charge_peak},
    };
}
} // namespace docenhance::bundle
