// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "continuous.hpp"

#include "contrast.hpp"
#include "denoising.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/bundle/fields.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "illumination.hpp"
#include "restoration.hpp"
#include "sharpening.hpp"

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
        {"restoration", bundle::restoration_fields(value.restoration)},
        {"contrast", bundle::contrast_fields(value.contrast)},
        {"sharpening", bundle::sharpen_fields(value.sharpening)},
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
        text += std::string(warning) + ": ";
        if (warning == "W_PROFILE_ASSUMED") {
            text += "Color interpretation assumes sRGB where declarations are absent; transfer=";
            text += report.assumed_transfer ? "assumed" : "declared";
            text += "; primaries=";
            text += report.assumed_primaries ? "assumed" : "declared";
            text += ". Check that these assumptions suit the source.\n";
        } else if (warning == "W_PROFILE_OVERRIDDEN") {
            text += "The selected sRGB policy overrides color declarations; "
                    "colors may change.\n";
        } else if (warning == "W_ALPHA_FLATTENED") {
            text += std::to_string(report.flattened_pixels) + " non-opaque pixels composited over ";
            text += value.alpha == image::AlphaPolicy::black ? "black" : "white";
            text += " in linear light. Output is opaque; original alpha cannot be recovered.\n";
        } else if (warning == "W_DEPTH_REDUCED") {
            text += std::to_string(report.source.depth.bits()) +
                    "-bit decoded samples reduced to " +
                    std::to_string(report.output.depth.bits()) +
                    " bits by request; discarded precision cannot be recovered.\n";
        }
    }
    text += illumination_text(value.illumination);
    text += denoising_text(value.denoising);
    text += restoration_text(value.restoration);
    text += contrast_text(value.contrast);
    text += sharpening_text(value.sharpening);
    return text;
}
} // namespace docenhance::report
