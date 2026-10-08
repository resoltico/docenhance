// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "observations.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/contract/cli_contract.hpp"
#include "docenhance/core/bundle_path.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"
#include "docenhance/methods/otsu.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/tvl1.hpp"

#include <cmath>
#include <cstddef>
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
        request.illumination());
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
        request.denoising());
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
        request.contrast());
}
bool partial_sharpen(const methods::SharpenReport& r, const ProcessRequest& request) {
    const auto* const method = std::get_if<methods::Unsharp>(&request.sharpening());
    if (method != nullptr ? (!r.requested || *r.requested != method->parameters())
                          : r.requested.has_value()) {
        return false;
    }
    if (!methods::valid_sharpen_observations(r)) {
        return false;
    }
    return r.complete ? methods::valid_sharpen(r, request.sharpening())
                      : (!r.requested || (r.status == methods::SharpenStatus::failed &&
                                          r.reason == methods::SharpenReason::processing_failure));
}
bool partial_contrast(const methods::ContrastReport& r, const ProcessRequest& request) {
    if (!contrast_request(r, request) || !methods::valid_contrast_observations(r)) {
        return false;
    }
    return r.complete ? methods::valid_contrast(r, request.contrast())
                      : (!r.requested || (r.status == methods::ContrastStatus::failed &&
                                          r.reason == methods::ContrastReason::processing_failure));
}
bool source_matches(const image::SourceDescription& source, image::RasterShape shape) {
    if (!image::valid_source_description(source)) {
        return false;
    }
    return std::visit(
        [shape](const auto& decoded) {
            using Source = std::decay_t<decltype(decoded)>;
            if (decoded.width != shape.width || decoded.height != shape.height) {
                return false;
            }
            if constexpr (std::is_same_v<Source, image::PngSource>) {
                const auto depth =
                    decoded.depth == image::word_bits ? image::word_bits : image::byte_bits;
                const bool color = decoded.color_type == 2 ||
                                   decoded.color_type == image::png_palette ||
                                   decoded.color_type == image::png_rgb_alpha;
                return shape.depth.bits() == depth && image::is_color(shape.model) == color;
            } else if constexpr (std::is_same_v<Source, image::TiffSource>) {
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
bool finite_illumination(const methods::IlluminationReport& report) {
    if (report.solver &&
        (!std::isfinite(report.solver->residual) || !std::isfinite(report.solver->tolerance))) {
        return false;
    }
    if (report.background_reference && !std::isfinite(*report.background_reference)) {
        return false;
    }
    if (report.morphology) {
        const auto& m = *report.morphology;
        if (!std::isfinite(m.sigma) || (m.analysis_fill && !std::isfinite(*m.analysis_fill)) ||
            (m.target && !std::isfinite(*m.target)) || !std::isfinite(m.background_q10) ||
            !std::isfinite(m.background_q50) || !std::isfinite(m.background_q90) ||
            !std::isfinite(m.background_min) || !std::isfinite(m.background_max) ||
            m.radius < methods::Morphology::min_radius ||
            m.radius > methods::Morphology::max_radius ||
            m.field_bytes > core::continuous_processing_budget ||
            m.preparation_charge_peak > core::continuous_processing_budget) {
            return false;
        }
    }
    if (!report.measurements) {
        return true;
    }
    const auto& m = *report.measurements;
    return std::isfinite(m.target) && std::isfinite(m.background_q10) &&
           std::isfinite(m.background_q50) && std::isfinite(m.background_q90) &&
           std::isfinite(m.luminance_q90) && std::isfinite(m.variation) &&
           std::isfinite(m.paper_fraction) && std::isfinite(m.dark_fraction);
}
bool illumination_shape(const methods::IlluminationReport& r) {
    return r.requested && std::holds_alternative<methods::MorphologyParameters>(*r.requested)
               ? methods::valid_morphology_observations(r)
               : !r.morphology;
}
bool partial_illumination(const methods::IlluminationReport& r, const ProcessRequest& request) {
    return illumination_request(r, request) && finite_illumination(r) && illumination_shape(r) &&
           static_cast<unsigned>(r.status) <=
               static_cast<unsigned>(methods::IlluminationStatus::failed) &&
           static_cast<unsigned>(r.reason) <=
               static_cast<unsigned>(methods::IlluminationReason::processing_failure) &&
           (!r.complete || r.status != methods::IlluminationStatus::failed) &&
           (r.complete || !r.requested || r.status == methods::IlluminationStatus::failed) &&
           r.eligible_samples <= image::source_pixels_max &&
           r.protected_samples <= image::source_pixels_max - r.eligible_samples &&
           r.evaluated_samples <= r.eligible_samples && r.changed_samples <= r.evaluated_samples &&
           r.gain_capped_samples <= r.evaluated_samples &&
           r.saturated_samples <= r.evaluated_samples && std::isfinite(r.min_gain) &&
           std::isfinite(r.max_gain) && r.min_gain >= 1 && r.max_gain >= r.min_gain &&
           r.max_gain <= methods::illumination_gain_limit;
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
               (r.complete ? methods::valid_denoising(r, request.denoising())
                           : r.status == methods::DenoiseStatus::failed &&
                                 r.reason == methods::DenoiseReason::processing_failure);
    }
    if (r.tvl1) {
        return false;
    }
    // The machine contract caps this reported processing charge; this does not cap RSS.
    if (r.preparation_charge_peak > core::continuous_processing_budget ||
        (r.native_calls == 0 && (r.native_reserved_peak != 0 || r.preparation_charge_peak != 0)) ||
        (r.native_calls != 0 &&
         (r.native_reserved_peak == 0 || r.preparation_charge_peak < r.native_reserved_peak))) {
        return false;
    }
    if (r.complete) {
        return methods::valid_denoising(r, request.denoising());
    }
    return r.requested ? r.status == methods::DenoiseStatus::failed &&
                             r.reason == methods::DenoiseReason::processing_failure &&
                             r.native_h == methods::nlm_native_strength(
                                               std::get<methods::NlmParameters>(*r.requested))
                       : r.status == methods::DenoiseStatus::disabled &&
                             r.reason == methods::DenoiseReason::none && r.native_h == 0;
}
} // namespace
bool valid_published(const PublishedBinary& value, const ProcessRequest& request) {
    const auto& method = std::get<methods::Binarization>(request.operation());
    if (value.otsu.has_value() != std::holds_alternative<methods::Otsu>(method) ||
        (value.otsu && !methods::valid_otsu(*value.otsu)) ||
        !publication(value.output, value.run, value.record, request)) {
        return false;
    }
    if (!value.source_decoding) {
        return true;
    }
    const auto* const png = std::get_if<image::PngSource>(&*value.source_decoding);
    return png != nullptr && image::valid_source_description(*value.source_decoding) &&
           png->color_type == 0 && png->depth <= image::byte_bits;
}
bool valid_published(const PublishedContinuous& value, const ProcessRequest& request) {
    const auto& c = value.conversion;
    const auto& light = value.illumination;
    const auto pixels = std::uint64_t{c.output.width} * c.output.height;
    if (!publication(value.output, value.run, value.record, request) ||
        !image::valid_conversion(c, std::get<image::Continuous>(request.operation())) ||
        !light.complete || !illumination_request(light, request) ||
        !methods::valid_illumination(light, {.width = c.output.width, .height = c.output.height},
                                     request.protection().has_value()) ||
        light.protected_samples > pixels ||
        light.eligible_samples != pixels - light.protected_samples ||
        !methods::valid_denoising(value.denoising, request.denoising()) ||
        !methods::valid_denoising_extent(value.denoising,
                                         {.width = c.output.width, .height = c.output.height}) ||
        value.denoising.eligible_samples != light.eligible_samples ||
        value.denoising.protected_samples != light.protected_samples ||
        !methods::valid_contrast(value.contrast, request.contrast()) ||
        value.contrast.eligible_samples != light.eligible_samples ||
        value.contrast.protected_samples != light.protected_samples ||
        !methods::valid_sharpen(value.sharpening, request.sharpening()) ||
        !methods::valid_sharpen_extent(value.sharpening,
                                       {.width = c.output.width, .height = c.output.height}) ||
        value.sharpening.eligible_samples != light.eligible_samples ||
        value.sharpening.protected_samples != light.protected_samples) {
        return false;
    }
    return !value.source_decoding || source_matches(*value.source_decoding, c.source);
}
bool valid_failure(const ProcessFailure& value, const ProcessRequest& request) {
    if (std::holds_alternative<methods::Binarization>(request.operation()) &&
        (value.illumination || value.denoising || value.contrast || value.sharpening)) {
        return false;
    }
    return value.error.valid_publication() &&
           (!value.illumination || partial_illumination(*value.illumination, request)) &&
           (!value.denoising || partial_denoising(*value.denoising, request)) &&
           (!value.contrast || partial_contrast(*value.contrast, request)) &&
           (!value.sharpening || partial_sharpen(*value.sharpening, request));
}
bool valid_verified(const Verified& value, const VerifyRequest& request) {
    if (value.directory != request.directory() ||
        !core::valid_hexadecimal(value.run, core::run_identity_hex_length) ||
        !core::valid_instant(value.recorded) || value.confirmed.empty() ||
        value.confirmed.size() > contract::response_confirmed_limit) {
        return false;
    }
    for (std::size_t i = 0; i < value.confirmed.size(); ++i) {
        const auto& entry = value.confirmed.at(i);
        if (!core::valid_bundle_path(entry.name) || !identity(entry.identity)) {
            return false;
        }
        for (std::size_t prior = 0; prior < i; ++prior) {
            if (value.confirmed.at(prior).name == entry.name) {
                return false;
            }
        }
    }
    return true;
}
} // namespace docenhance::app
