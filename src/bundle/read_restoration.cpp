// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/restoration.hpp"
#include "read_fields.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <utility>
#include <variant>
namespace docenhance::bundle {
namespace {
core::Result<methods::Psf> recorded_psf_request(const RecordJson& p) {
    const auto kind = record_text(record_field(p, "kind"));
    if (kind == "gaussian") {
        return methods::GaussianPsf{.sigma = record_number(record_field(p, "sigma"))};
    }
    if (kind == "motion") {
        return methods::MotionPsf{
            .length = record_number(record_field(p, "length")),
            .angle = record_number(record_field(p, "angle")),
        };
    }
    if (kind == "kernel") {
        return methods::FilePsf{.path = record_text(record_field(p, "path"))};
    }
    return core::failure(core::ErrorCode::input, "Invalid recorded PSF selector");
}
core::Result<methods::ResolvedPsf> recorded_psf(const RecordJson& v) {
    methods::ResolvedPsf psf;
    psf.width = static_cast<std::uint32_t>(
        record_integer(record_field(v, "width"), methods::psf_maximum_side));
    psf.height = static_cast<std::uint32_t>(
        record_integer(record_field(v, "height"), methods::psf_maximum_side));
    const auto& coefficients = record_field(v, "coefficients");
    if (!coefficients.is_array() || coefficients.size() != std::uint64_t{psf.width} * psf.height) {
        return core::failure(core::ErrorCode::input, "Invalid recorded PSF coefficient extent");
    }
    psf.coefficients.reserve(coefficients.size());
    for (const auto& coefficient : coefficients) {
        psf.coefficients.push_back(record_number(coefficient));
    }
    const auto& centroid = record_field(v, "centroid");
    psf.centroid_x = record_number(record_field(centroid, "x"));
    psf.centroid_y = record_number(record_field(centroid, "y"));
    const auto& identity = record_field(v, "source_identity");
    if (!identity.is_null()) {
        psf.source_identity = methods::PsfIdentity{
            .path = record_text(record_field(identity, "path")),
            .identity = record_identity(identity),
        };
    }
    const auto valid = methods::validate_psf(psf);
    if (!valid) {
        return core::failure(core::ErrorCode::input, "Invalid recorded normalized PSF");
    }
    return psf;
}
void read_counters(const RecordJson& v, methods::RestorationReport& r) {
    constexpr auto bound = image::source_pixels_max;
    r.eligible_samples = record_integer(record_field(v, "eligible_samples"), bound);
    r.protected_samples = record_integer(record_field(v, "protected_samples"), bound);
    r.context_samples = record_integer(record_field(v, "context_samples"), bound);
    r.evaluated_samples = record_integer(record_field(v, "evaluated_samples"), bound);
    r.corrected_samples = record_integer(record_field(v, "corrected_samples"), bound);
    r.changed_samples = record_integer(record_field(v, "changed_samples"), bound);
    r.raw_low_samples = record_integer(record_field(v, "raw_low_samples"), bound);
    r.raw_high_samples = record_integer(record_field(v, "raw_high_samples"), bound);
    r.blended_low_samples = record_integer(record_field(v, "blended_low_samples"), bound);
    r.blended_high_samples = record_integer(record_field(v, "blended_high_samples"), bound);
    r.guard = static_cast<std::uint32_t>(
        record_integer(record_field(v, "guard"), methods::fft_max_guard));
    r.fft_width =
        static_cast<std::uint32_t>(record_integer(record_field(v, "fft_width"), UINT32_MAX));
    r.fft_height =
        static_cast<std::uint32_t>(record_integer(record_field(v, "fft_height"), UINT32_MAX));
    r.native_calls = static_cast<std::uint32_t>(
        record_integer(record_field(v, "native_calls"), methods::restoration_native_calls));
    r.native_reserved_bytes = record_integer(record_field(v, "native_reserved_bytes"),
                                             core::continuous_processing_budget);
    r.preparation_charge_peak = record_integer(record_field(v, "preparation_charge_peak"),
                                               core::continuous_processing_budget);
    const auto& mean = record_field(v, "padded_mean");
    if (!mean.is_null()) {
        r.padded_mean = record_number(mean);
    }
    const auto& warnings = record_field(v, "warnings");
    if (warnings.is_array()) {
        for (const auto& warning : warnings) {
            const auto text = record_text(warning);
            r.inference_warning |= text == "W_RESTORATION_INFERENCE";
            r.after_transform_warning |= text == "W_PSF_AFTER_TRANSFORM";
            r.off_center_warning |= text == "W_PSF_OFF_CENTER";
        }
    }
}
void read_states(const RecordJson& v, methods::RestorationReport& r) {
    for (const auto status : std::array{
             methods::RestorationStatus::disabled,
             methods::RestorationStatus::no_change,
             methods::RestorationStatus::applied,
             methods::RestorationStatus::failed,
         }) {
        if (methods::status_name(status) == record_text(record_field(v, "status"))) {
            r.status = status;
        }
    }
    for (const auto reason : std::array{
             methods::RestorationReason::none,
             methods::RestorationReason::zero_blend,
             methods::RestorationReason::no_eligible_samples,
             methods::RestorationReason::no_effect,
             methods::RestorationReason::processing_failure,
         }) {
        if (methods::reason_name(reason) == record_text(record_field(v, "reason"))) {
            r.reason = reason;
        }
    }
    r.complete = record_boolean(record_field(v, "complete"));
}
} // namespace
core::Result<void> validate_restoration_claims(const RecordJson& document, DeclaredBundle& d,
                                               const Operation& operation,
                                               const methods::IlluminationReport& light) {
    const auto& v = record_field(record_field(document, "execution"), "restoration");
    methods::RestorationReport r;
    methods::Restoration method = methods::RestorationOff{};
    const auto& p = record_field(v, "parameters");
    if (!p.is_null()) {
        auto psf = recorded_psf_request(record_field(p, "psf"));
        if (!psf) {
            return std::unexpected(psf.error());
        }
        auto selected = methods::Wiener::create({
            .psf = std::move(*psf),
            .k = record_number(record_field(p, "k")),
            .blend = record_number(record_field(p, "blend")),
        });
        if (!selected) {
            return core::failure(core::ErrorCode::input, "Invalid recorded restoration parameters");
        }
        method = *selected;
        r.requested = selected->parameters();
        r.status = methods::RestorationStatus::failed;
    }
    const auto& psf = record_field(v, "psf");
    if (!psf.is_null()) {
        auto loaded = recorded_psf(psf);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        r.psf = std::move(*loaded);
    }
    read_states(v, r);
    read_counters(v, r);
    if (!methods::valid_restoration(r, method) || RecordJson(restoration_fields(r)) != v ||
        RecordJson(restoration_request_fields(r)) !=
            record_field(record_field(document, "request"), "restoration")) {
        return core::failure(core::ErrorCode::input, "Invalid recorded restoration observations");
    }
    if (std::holds_alternative<methods::Binarization>(operation)) {
        if (r.requested || r.eligible_samples != 0 || r.protected_samples != 0) {
            return core::failure(core::ErrorCode::input, "Binary record cannot claim restoration");
        }
    } else {
        const auto& conversion = record_field(record_field(document, "execution"), "conversion");
        auto converted = record_conversion(conversion);
        if (!converted ||
            !methods::valid_restoration_extent(
                r, {.width = d.output.shape.width, .height = d.output.shape.height}) ||
            r.eligible_samples != light.eligible_samples ||
            r.protected_samples != light.protected_samples ||
            r.after_transform_warning !=
                (r.requested.has_value() &&
                 (converted->orientation != image::Orientation::normal() ||
                  converted->rotation.degrees() != 0 || light.changed_samples != 0 ||
                  d.denoising.changed_samples != 0))) {
            return core::failure(core::ErrorCode::input,
                                 "Inconsistent restoration extent or preceding transformations");
        }
    }
    d.restoration = std::move(r);
    return {};
}
} // namespace docenhance::bundle
