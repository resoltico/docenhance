// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "observations.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/contract/cli_contract.hpp"
#include "docenhance/contract/utf8.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"

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
    return separator && suffix.substr(1) == "result.png" && contract::valid_path(output) &&
           core::valid_hexadecimal(run, core::run_identity_hex_length) && identity(record);
}
bool illumination_request(const methods::IlluminationReport& report,
                          const ProcessRequest& request) {
    const auto* const selected = std::get_if<methods::Surface>(&request.illumination());
    return selected == nullptr ? !report.requested
                               : report.requested && *report.requested == selected->parameters();
}
bool denoising_request(const methods::DenoisingReport& report, const ProcessRequest& request) {
    const auto* const selected = std::get_if<methods::Nlm>(&request.denoising());
    return selected == nullptr ? !report.requested
                               : report.requested && *report.requested == selected->parameters();
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
    if (!report.measurements) {
        return true;
    }
    const auto& m = *report.measurements;
    return std::isfinite(m.target) && std::isfinite(m.background_q10) &&
           std::isfinite(m.background_q50) && std::isfinite(m.background_q90) &&
           std::isfinite(m.luminance_q90) && std::isfinite(m.variation) &&
           std::isfinite(m.paper_fraction) && std::isfinite(m.dark_fraction);
}
bool partial_illumination(const methods::IlluminationReport& r, const ProcessRequest& request) {
    return illumination_request(r, request) && finite_illumination(r) &&
           static_cast<unsigned>(r.status) <=
               static_cast<unsigned>(methods::SurfaceStatus::failed) &&
           static_cast<unsigned>(r.reason) <=
               static_cast<unsigned>(methods::SurfaceReason::processing_failure) &&
           (!r.complete || r.status != methods::SurfaceStatus::failed) &&
           (r.complete || !r.requested || r.status == methods::SurfaceStatus::failed) &&
           r.eligible_samples <= image::source_pixels_max &&
           r.protected_samples <= image::source_pixels_max - r.eligible_samples &&
           r.evaluated_samples <= r.eligible_samples && r.changed_samples <= r.evaluated_samples &&
           r.gain_capped_samples <= r.evaluated_samples &&
           r.saturated_samples <= r.evaluated_samples && std::isfinite(r.min_gain) &&
           std::isfinite(r.max_gain) && r.min_gain >= 1 && r.max_gain >= r.min_gain &&
           r.max_gain <= methods::surface_gain_limit;
}
bool partial_denoising(const methods::DenoisingReport& r, const ProcessRequest& request) {
    if (!denoising_request(r, request) || r.eligible_samples > image::source_pixels_max ||
        r.protected_samples > image::source_pixels_max - r.eligible_samples ||
        r.evaluated_samples > r.eligible_samples || r.corrected_samples > r.evaluated_samples ||
        r.changed_samples > r.corrected_samples || !std::isfinite(r.native_h)) {
        return false;
    }
    // The machine contract caps this reported processing charge; this does not cap RSS.
    constexpr std::uint64_t reported_charge_limit = std::uint64_t{1024} * 1024 * 1024;
    if (r.preparation_charge_peak > reported_charge_limit ||
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
                             r.native_h == methods::nlm_native_strength(*r.requested)
                       : r.status == methods::DenoiseStatus::disabled &&
                             r.reason == methods::DenoiseReason::none && r.native_h == 0;
}
bool relative_name(std::string_view name) {
    if (!contract::valid_path(name) || name.front() == '/' || name.contains('\\') ||
        name.contains(':')) {
        return false;
    }
    while (!name.empty()) {
        const auto slash = name.find('/');
        const auto part = name.substr(0, slash);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (slash == std::string_view::npos) {
            return true;
        }
        name.remove_prefix(slash + 1);
    }
    return false;
}
} // namespace
bool valid_published(const PublishedBinary& value, const ProcessRequest& request) {
    if (!publication(value.output, value.run, value.record, request)) {
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
        value.denoising.protected_samples != light.protected_samples) {
        return false;
    }
    return !value.source_decoding || source_matches(*value.source_decoding, c.source);
}
bool valid_failure(const ProcessFailure& value, const ProcessRequest& request) {
    if (std::holds_alternative<methods::Binarization>(request.operation()) &&
        (value.illumination || value.denoising)) {
        return false;
    }
    return value.error.valid_publication() &&
           (!value.illumination || partial_illumination(*value.illumination, request)) &&
           (!value.denoising || partial_denoising(*value.denoising, request));
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
        if (!relative_name(entry.name) || !identity(entry.identity)) {
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
