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
    return text;
}
} // namespace docenhance::report
