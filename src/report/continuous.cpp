// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "continuous.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "illumination.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace docenhance::report {
namespace {
using Json = nlohmann::ordered_json;
Json dimensions(image::RasterShape shape) {
    return {
        {"width", shape.width},
        {"height", shape.height},
        {"channels", image::components(shape.model)},
        {"bit_depth", shape.depth},
    };
}
Json warnings(const image::ConversionReport& value) {
    Json result = Json::array();
    if (value.assumed_transfer || value.assumed_primaries) {
        result.push_back("W_PROFILE_ASSUMED");
    }
    if (value.interpretation == image::Interpretation::overridden_srgb) {
        result.push_back("W_PROFILE_OVERRIDDEN");
    }
    if (value.flattened_pixels != 0) {
        result.push_back("W_ALPHA_FLATTENED");
    }
    if (value.depth_reduced) {
        result.push_back("W_DEPTH_REDUCED");
    }
    return result;
}
} // namespace
nlohmann::ordered_json continuous_fields(const app::ContinuousProcessed& value) {
    const auto& report = value.conversion;
    Json resolution = nullptr;
    if (report.resolution) {
        resolution = {{"x_ppm", report.resolution->x}, {"y_ppm", report.resolution->y}};
    }
    return {
        {"operation", "continuous"},
        {"illumination", illumination_fields(value.illumination)},
        {"output", value.output},
        {"publication", "completed"},
        {
            "conversion",
            {
                {"decoded_input", dimensions(report.source)},
                {"encoded_output", dimensions(report.output)},
                {"profile_decision", image::interpretation_name(report.interpretation)},
                {"assumed_transfer", report.assumed_transfer},
                {"assumed_primaries", report.assumed_primaries},
                {"source_orientation", report.orientation},
                {"resolution", resolution},
                {"alpha_flattened_pixels", report.flattened_pixels},
                {"clipped_components", report.clipped_components},
                {"depth_reduced", report.depth_reduced},
                {"verified", report.verified},
                {"warnings", warnings(report)},
            },
        },
    };
}
std::string continuous_text(const app::ContinuousProcessed& value) {
    const auto& report = value.conversion;
    std::string text = "Wrote verified continuous-tone PNG: " + value.output + "\n";
    text += std::to_string(report.output.depth) + " bits; " +
            std::to_string(image::components(report.output.model)) + " channels; interpretation: " +
            std::string(image::interpretation_name(report.interpretation)) + "\n";
    for (const auto& warning : warnings(report)) {
        text += warning.get<std::string>() + "\n";
    }
    text += illumination_text(value.illumination);
    return text;
}
} // namespace docenhance::report
