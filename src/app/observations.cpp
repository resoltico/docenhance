// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "observations.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/otsu.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "restoration.hpp"
#include "sharpening.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
namespace docenhance::app {
namespace {
bool identity(const core::ContentIdentity& value) {
    return value.bytes != 0 && core::valid_hexadecimal(value.sha256, core::sha256_hex_length);
}
bool publication(std::string_view output, std::string_view run, const core::ContentIdentity& record,
                 const ProcessRequest& request) {
    const auto& directory = request.output_directory();
    if (output.size() <= directory.size() || !output.starts_with(directory)) {
        return false;
    }
    const auto suffix = output.substr(directory.size());
#ifdef _WIN32
    const bool separator = suffix.front() == '/' || suffix.front() == '\\';
#else
    const bool separator = suffix.front() == '/';
#endif
    return separator && suffix.substr(1) == "result.png" && core::valid_path(output) &&
           core::valid_hexadecimal(run, core::run_identity_hex_length) && identity(record);
}
bool illumination_request(const methods::IlluminationReport& report,
                          const ProcessRequest& request) {
    return std::visit(
        [&](const auto& selected) {
            using Method = std::decay_t<decltype(selected)>;
            if constexpr (std::is_same_v<Method, methods::IlluminationOff>) {
                return !report.requested;
            } else {
                return report.requested &&
                       *report.requested == methods::IlluminationParameters{selected.parameters()};
            }
        },
        std::get<app::ContinuousPlan>(request.plan()).photometric.illumination);
}
bool denoising_request(const methods::DenoisingReport& report, const ProcessRequest& request) {
    return std::visit(
        [&](const auto& selected) {
            using Method = std::decay_t<decltype(selected)>;
            if constexpr (std::is_same_v<Method, methods::DenoisingOff>) {
                return !report.requested;
            } else {
                return report.requested &&
                       *report.requested == methods::DenoisingParameters{selected.parameters()};
            }
        },
        std::get<app::ContinuousPlan>(request.plan()).photometric.denoising);
}
bool contrast_request(const methods::ContrastReport& r, const ProcessRequest& request) {
    return std::visit(
        [&](const auto& selected) {
            using M = std::decay_t<decltype(selected)>;
            if constexpr (std::is_same_v<M, methods::ContrastOff>) {
                return !r.requested;
            } else {
                return r.requested &&
                       *r.requested == methods::ContrastParameters{selected.parameters()};
            }
        },
        std::get<app::ContinuousPlan>(request.plan()).photometric.contrast);
}
bool partial_contrast(const methods::ContrastReport& r, const ProcessRequest& request) {
    if (!contrast_request(r, request) || !methods::valid_contrast_observations(r)) {
        return false;
    }
    return r.complete() ? methods::valid_contrast(
                              r, std::get<app::ContinuousPlan>(request.plan()).photometric.contrast)
                        : methods::valid_progress(r);
}
bool source_matches(const image::SourceFacts& source, image::RasterShape shape) {
    if (!image::valid_source_facts(source)) {
        return false;
    }
    return std::visit(
        [shape](const auto& decoded) {
            using Source = std::decay_t<decltype(decoded)>;
            if (decoded.width != shape.width || decoded.height != shape.height) {
                return false;
            }
            if constexpr (std::is_same_v<Source, image::PngFacts>) {
                const auto depth =
                    decoded.depth == image::word_bits ? image::word_bits : image::byte_bits;
                const bool color =
                    decoded.color_type == 2 ||
                    decoded.color_type == static_cast<unsigned>(image::PngColor::palette) ||
                    decoded.color_type == static_cast<unsigned>(image::PngColor::rgb_alpha);
                return shape.depth.bits() == depth && image::is_color(shape.model) == color;
            } else if constexpr (std::is_same_v<Source, image::TiffFacts>) {
                return shape == image::decoded_tiff_shape(decoded);
            } else {
                const auto model = decoded.color == image::JpegColor::gray
                                       ? image::SampleModel::gray
                                       : image::SampleModel::rgb;
                return shape.depth == image::SampleDepth::byte() && shape.model == model;
            }
        },
        source);
}
bool partial_illumination(const methods::IlluminationReport& report,
                          const ProcessRequest& request) {
    return illumination_request(report, request) &&
           methods::valid_illumination_observations(report);
}
bool partial_native(const methods::DenoisingReport& r) {
    if (r.status == methods::DenoiseStatus::failed &&
        r.reason != methods::DenoiseReason::processing_failure) {
        return false;
    }
    // The machine contract caps this reported processing charge; this does not cap RSS.
    if (r.preparation_charge_peak > core::continuous_processing_budget ||
        (r.native_calls == 0 && (r.native_reserved_peak != 0 || r.preparation_charge_peak != 0)) ||
        (r.native_calls != 0 &&
         (r.native_reserved_peak == 0 || r.preparation_charge_peak < r.native_reserved_peak))) {
        return false;
    }
    if ((r.progress == methods::StageProgress::prepared ||
         r.progress == methods::StageProgress::evaluating) &&
        r.native_calls == 0) {
        return false;
    }
    return true;
}
bool partial_denoising(const methods::DenoisingReport& r, const ProcessRequest& request) {
    if (!denoising_request(r, request) || r.eligible_samples > image::source_pixels_max ||
        r.protected_samples > image::source_pixels_max - r.eligible_samples ||
        r.evaluated_samples > r.eligible_samples || r.corrected_samples > r.evaluated_samples ||
        r.changed_samples > r.corrected_samples || !std::isfinite(r.native_h)) {
        return false;
    }
    if (r.requested && std::holds_alternative<methods::Tvl1Parameters>(*r.requested)) {
        return methods::valid_tvl1_observations(r) &&
               (r.complete()
                    ? methods::valid_denoising(
                          r, std::get<app::ContinuousPlan>(request.plan()).photometric.denoising)
                    : methods::valid_progress(r));
    }
    if (r.tvl1) {
        return false;
    }
    if (!partial_native(r)) {
        return false;
    }
    if (r.complete()) {
        return methods::valid_denoising(
            r, std::get<app::ContinuousPlan>(request.plan()).photometric.denoising);
    }
    return r.requested ? methods::valid_progress(r) &&
                             r.native_h == methods::nlm_native_strength(
                                               std::get<methods::NlmParameters>(*r.requested))
                       : r.status == methods::DenoiseStatus::disabled &&
                             r.reason == methods::DenoiseReason::none && r.native_h == 0;
}
} // namespace
bool valid_published(const PublishedBinary& value, const ProcessRequest& request) {
    const auto& method = std::get<app::BinaryPlan>(request.plan()).operation;
    if (value.rotation != request.rotation() ||
        value.otsu.has_value() != std::holds_alternative<methods::Otsu>(method) ||
        (value.otsu && !methods::valid_otsu(*value.otsu)) ||
        !publication(value.output, value.run, value.record, request)) {
        return false;
    }
    if (!value.source_decoding) {
        return true;
    }
    const auto* const png = std::get_if<image::PngFacts>(&*value.source_decoding);
    return png != nullptr && image::valid_source_facts(*value.source_decoding) &&
           png->color_type == 0 && png->depth <= image::byte_bits;
}
bool valid_published(const PublishedContinuous& value, const ProcessRequest& request) {
    const auto& plan = std::get<ContinuousPlan>(request.plan());
    const auto& stages = value.stages;
    const auto& c = value.conversion;
    const auto& light = stages.illumination;
    const auto pixels = std::uint64_t{c.output.width} * c.output.height;
    if (value.alpha != plan.operation.parameters().alpha || c.rotation != request.rotation() ||
        !publication(value.output, value.run, value.record, request) ||
        !image::valid_conversion(c, plan.operation) || !light.complete() ||
        !illumination_request(light, request) ||
        !methods::valid_illumination(light, {.width = c.output.width, .height = c.output.height},
                                     plan.protection.has_value()) ||
        light.protected_samples > pixels ||
        light.eligible_samples != pixels - light.protected_samples ||
        !methods::valid_denoising(stages.denoising, plan.photometric.denoising) ||
        !methods::valid_denoising_extent(stages.denoising,
                                         {.width = c.output.width, .height = c.output.height}) ||
        stages.denoising.eligible_samples != light.eligible_samples ||
        stages.denoising.protected_samples != light.protected_samples ||
        !methods::valid_restoration(stages.restoration, plan.photometric.restoration) ||
        !methods::valid_restoration_extent(stages.restoration,
                                           {.width = c.output.width, .height = c.output.height}) ||
        stages.restoration.eligible_samples != light.eligible_samples ||
        stages.restoration.protected_samples != light.protected_samples ||
        !restoration_prefix(stages.restoration, c.orientation, c.rotation, light,
                            stages.denoising) ||
        !methods::valid_contrast(stages.contrast, plan.photometric.contrast) ||
        stages.contrast.eligible_samples != light.eligible_samples ||
        stages.contrast.protected_samples != light.protected_samples ||
        !methods::valid_sharpen(stages.sharpening, plan.photometric.sharpening) ||
        !methods::valid_sharpen_extent(stages.sharpening,
                                       {.width = c.output.width, .height = c.output.height}) ||
        stages.sharpening.eligible_samples != light.eligible_samples ||
        stages.sharpening.protected_samples != light.protected_samples) {
        return false;
    }
    return !value.source_decoding || source_matches(*value.source_decoding, c.source);
}
bool valid_partial_stages(const methods::StageObservations& stages, const ProcessRequest& request) {
    const auto& plan = std::get<ContinuousPlan>(request.plan());
    // Unstarted reports have initial facts; existing validators check attempted work.
    std::optional<methods::StageObservations> initial;
    const auto expected = [&] -> const methods::StageObservations& {
        if (!initial) {
            initial.emplace(initial_stages(plan.photometric));
        }
        return *initial;
    };
    return (stages.illumination.progress == methods::StageProgress::not_started
                ? stages.illumination == expected().illumination
                : partial_illumination(stages.illumination, request)) &&
           (stages.denoising.progress == methods::StageProgress::not_started
                ? stages.denoising == expected().denoising
                : partial_denoising(stages.denoising, request)) &&
           (stages.restoration.progress == methods::StageProgress::not_started
                ? stages.restoration == expected().restoration
                : partial_restoration(stages.restoration, request)) &&
           (stages.contrast.progress == methods::StageProgress::not_started
                ? stages.contrast == expected().contrast
                : partial_contrast(stages.contrast, request)) &&
           (stages.sharpening.progress == methods::StageProgress::not_started
                ? stages.sharpening == expected().sharpening
                : partial_sharpen(stages.sharpening, request));
}
} // namespace docenhance::app
