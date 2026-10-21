// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "continuous.hpp"

#include "contrast.hpp"
#include "denoising.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/scalar.hpp"
#include "docenhance/report/warnings.hpp"
#include "illumination.hpp"
#include "restoration.hpp"
#include "sharpening.hpp"

#include <expected>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::report {
namespace {
core::Result<std::string> interpretation_text(const record::RunRecord& value) {
    const auto& facts = value.execution.interpretation;
    auto alpha =
        std::visit([](const auto& output) { return record::ScalarCodec::encode(output.alpha); },
                   value.request.output);
    if (!alpha) {
        return std::unexpected(std::move(alpha.error()));
    }
    std::string text = std::string{"Interpretation: transfer="} +
                       (facts.assumed_transfer ? "assumed" : "declared") +
                       "; primaries=" + (facts.assumed_primaries ? "assumed" : "declared") +
                       "; orientation=" + std::to_string(facts.orientation.code()) +
                       "; rotation=" + std::to_string(facts.rotation.degrees()) + "\n";
    if (facts.flattened_pixels != 0) {
        text += "Alpha: " + std::to_string(facts.flattened_pixels) +
                " non-opaque pixels composited over " + alpha->get<std::string>() +
                "; original alpha cannot be recovered\n";
    }
    if (facts.depth_reduced) {
        text += "Precision: " + std::to_string(facts.decoded.depth.bits()) +
                "-bit decoded samples reduced to " + std::to_string(facts.output.depth.bits()) +
                " bits\n";
    }
    return text;
}
} // namespace
core::Result<std::string> warnings_text(const RunWarnings& warnings) {
    std::string text;
    for (const auto warning : warnings) {
        if (!warning) {
            continue;
        }
        auto name = record::ScalarCodec::encode(*warning);
        if (!name) {
            return std::unexpected(std::move(name.error()));
        }
        const auto explanation = warning_explanation(*warning);
        if (explanation) {
            text += name->get<std::string>();
            text += ": ";
            text += *explanation;
            text += '\n';
        }
    }
    return text;
}
core::Result<std::string> completed_text(const methods::StageReports& facts) {
    auto illumination = illumination_text(facts.illumination);
    if (!illumination) {
        return std::unexpected(std::move(illumination.error()));
    }
    auto denoising = denoising_text(facts.denoising);
    if (!denoising) {
        return std::unexpected(std::move(denoising.error()));
    }
    auto restoration = restoration_text(facts.restoration);
    if (!restoration) {
        return std::unexpected(std::move(restoration.error()));
    }
    auto contrast = contrast_text(facts.contrast);
    if (!contrast) {
        return std::unexpected(std::move(contrast.error()));
    }
    auto sharpening = sharpening_text(facts.sharpening);
    if (!sharpening) {
        return std::unexpected(std::move(sharpening.error()));
    }
    std::string text = *illumination + *denoising + *restoration + *contrast + *sharpening;
    if (facts.binarization) {
        auto encoded = record::Codec::encode(*facts.binarization);
        if (!encoded) {
            return std::unexpected(std::move(encoded.error()));
        }
        text += std::visit(
            [&](const auto& actual) {
                using Facts = std::decay_t<decltype(actual)>;
                std::string classification =
                    "Binarization " + std::string(core::WireTag<Facts>::value) + ": " +
                    encoded->at("disposition").get<std::string>() + " (" +
                    encoded->at("reason").get<std::string>() +
                    "); black=" + std::to_string(actual.black_samples) +
                    "; white=" + std::to_string(actual.white_samples) + "\n";
                if constexpr (std::is_same_v<Facts, methods::OtsuFacts>) {
                    classification +=
                        "Otsu: threshold bin=" + std::to_string(actual.threshold_bin) +
                        "; single-bin fallback=" + encoded->at("single_bin_fallback").dump() + "\n";
                }
                return classification;
            },
            *facts.binarization);
    }
    if (!methods::validate(facts)) {
        return core::failure(core::ErrorCode::invariant, "Invalid completed stage facts");
    }
    // An absent slot may be disabled or unstarted; it is never given a completion claim.
    return text.empty() ? "Completed stage facts: none reported\n"
                        : "Completed stage facts:\n" + text;
}
core::Result<std::string> diagnostics_text(const app::Diagnostics& diagnostics) {
    return "Execution: threads=" + std::to_string(diagnostics.threads) +
           "; memory limit=" + std::to_string(diagnostics.memory_limit_mib) +
           " MiB; charged peak=" + std::to_string(diagnostics.charged_peak_bytes) + " bytes\n";
}
core::Result<std::string> continuous_text(const app::Published& value) {
    const auto& interpretation = value.record.execution.interpretation;
    std::string text = "Wrote verified PNG: " + value.output + "\n";
    text += std::to_string(interpretation.output.depth.bits()) + " bits; " +
            std::to_string(image::components(interpretation.output.model)) + " channels\n";
    text += "run " + value.record.context.identity + " recorded " + value.record.context.recorded +
            "\n";
    text += "record SHA-256 " + value.record_identity.sha256 + "\n";
    auto interpreted = interpretation_text(value.record);
    if (!interpreted) {
        return std::unexpected(std::move(interpreted.error()));
    }
    text += *interpreted;
    auto completed = completed_text(value.record.execution);
    if (!completed) {
        return std::unexpected(std::move(completed.error()));
    }
    text += *completed;
    auto diagnostics = diagnostics_text(value.diagnostics);
    if (!diagnostics) {
        return std::unexpected(std::move(diagnostics.error()));
    }
    text += *diagnostics;
    auto warnings = warnings_text(run_warnings(value.record));
    if (!warnings) {
        return std::unexpected(std::move(warnings.error()));
    }
    text += *warnings;
    return text;
}
} // namespace docenhance::report
