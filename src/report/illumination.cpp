// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "illumination.hpp"

#include "docenhance/bundle/fields.hpp"
#include "docenhance/methods/illumination.hpp"

#include <string>
namespace docenhance::report {
std::string illumination_text(const methods::IlluminationReport& r) {
    std::string text = "Illumination: " + std::string(bundle::status_name(r.status)) + " (" +
                       std::string(bundle::reason_name(r.reason)) +
                       "); protected=" + std::to_string(r.protected_samples) + "\n";
    if (r.solver) {
        text += "I01 solver: iterations=" + std::to_string(r.solver->iterations) +
                "; residual=" + std::to_string(r.solver->residual) + "\n";
    }
    if (r.morphology) {
        const auto& m = r.morphology.value();
        text += "I02 field: radius=" + std::to_string(m.radius) +
                "; sigma=" + std::to_string(m.sigma) +
                "; Gaussian radius=" + std::to_string(m.gaussian_radius) +
                "; samples=" + std::to_string(m.count) +
                "; charged peak=" + std::to_string(m.preparation_charge_peak) + " bytes\n";
        if (m.target && m.analysis_fill) {
            text += "I02 analysis: fill=" + std::to_string(m.analysis_fill.value()) +
                    "; target=" + std::to_string(m.target.value()) + "\n";
        }
    }
    return text;
}
} // namespace docenhance::report
