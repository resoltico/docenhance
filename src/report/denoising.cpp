// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "denoising.hpp"

#include "docenhance/methods/denoising.hpp"

#include <nlohmann/json.hpp>
#include <string>
namespace docenhance::report {
namespace {
std::string decimal(double value) {
    // Use the machine renderer's round-trip precision for small stopping residuals too.
    return nlohmann::ordered_json(value).dump();
}
} // namespace
std::string denoising_text(const methods::DenoisingReport& report) {
    std::string text = "Denoising: " + std::string(methods::status_name(report.status)) + " (" +
                       std::string(methods::reason_name(report.reason)) +
                       "); changed=" + std::to_string(report.changed_samples) + "\n";
    if (report.tvl1) {
        const auto& tv = *report.tvl1;
        text += "TV-L1: iterations=" + std::to_string(tv.iterations) +
                "; primal update=" + decimal(tv.primal_update) +
                "; dual update=" + decimal(tv.dual_update) +
                "; charged peak=" + std::to_string(report.preparation_charge_peak) + " bytes\n";
        if (tv.objective_start && tv.objective_end) {
            text += "TV-L1 objective: " + decimal(*tv.objective_start) + " -> " +
                    decimal(*tv.objective_end) + "\n";
        }
        if (tv.stop == methods::Tvl1Stop::iteration_limit) {
            text += "TV-L1 stopping: iteration_limit\nW_TV_ITERATION_LIMIT\n";
        }
        if (tv.stop == methods::Tvl1Stop::tolerance_met) {
            text += "TV-L1 stopping: tolerance_met\n";
        }
    }
    return text;
}
} // namespace docenhance::report
