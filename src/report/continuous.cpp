// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "continuous.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/bundle/fields.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"
#include "illumination.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace docenhance::report {
nlohmann::ordered_json continuous_fields(const app::PublishedContinuous& value) {
    // Publication state belongs to the response alone: the persistent record is written before
    // the commit point and cannot assert that publication succeeded.
    return {
        {"operation", "continuous"},
        {"illumination", bundle::illumination_fields(value.illumination)},
        {"denoising", bundle::denoising_fields(value.denoising)},
        {"output", value.output},
        {"publication", "completed"},
        {"conversion", bundle::conversion_fields(value.conversion)},
        {"record", bundle::record_fields(value.run, value.record)},
        {
            "source_decoding",
            value.source_decoding ? bundle::source_fields(*value.source_decoding)
                                  : nlohmann::ordered_json(nullptr),
        },
    };
}
std::string continuous_text(const app::PublishedContinuous& value) {
    const auto& report = value.conversion;
    std::string text = "Wrote verified continuous-tone PNG: " + value.output + "\n";
    text += std::to_string(report.output.depth.bits()) + " bits; " +
            std::to_string(image::components(report.output.model)) + " channels; interpretation: " +
            std::string(image::interpretation_name(report.interpretation)) + "\n";
    for (const auto warning : bundle::conversion_warnings(report)) {
        text += std::string(warning) + "\n";
    }
    text += illumination_text(value.illumination);
    text += "Denoising: " + std::string(methods::status_name(value.denoising.status)) + " (" +
            std::string(methods::reason_name(value.denoising.reason)) +
            "); changed=" + std::to_string(value.denoising.changed_samples) + "\n";
    return text;
}
} // namespace docenhance::report
