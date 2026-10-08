// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"
#include "docenhance/methods/restoration.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <type_traits>
#include <variant>
namespace docenhance::bundle {
using Json = nlohmann::ordered_json;
namespace {
Json psf_request(const methods::Psf& value) {
    return std::visit(
        [](const auto& p) -> Json {
            using P = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<P, methods::GaussianPsf>) {
                return {{"kind", "gaussian"}, {"sigma", p.sigma}};
            } else if constexpr (std::is_same_v<P, methods::MotionPsf>) {
                return {{"kind", "motion"}, {"length", p.length}, {"angle", p.angle}};
            } else {
                return {{"kind", "kernel"}, {"path", p.path}};
            }
        },
        value);
}
Json psf_fields(const std::optional<methods::ResolvedPsf>& value) {
    if (!value) {
        return nullptr;
    }
    Json identity = nullptr;
    if (value->source_identity) {
        const auto& source = *value->source_identity;
        identity = {
            {"path", source.path},
            {"sha256", source.identity.sha256},
            {"bytes", source.identity.bytes},
        };
    }
    return {
        {"width", value->width},
        {"height", value->height},
        {"coefficients", value->coefficients},
        {"centroid", {{"x", value->centroid_x}, {"y", value->centroid_y}}},
        {"source_identity", identity},
    };
}
Json fraction(std::uint64_t low, std::uint64_t high, std::uint64_t evaluated) {
    return evaluated == 0 ? Json(nullptr)
                          : Json(static_cast<double>(low + high) / static_cast<double>(evaluated));
}
} // namespace
Json restoration_request_fields(const methods::RestorationReport& r) {
    Json method = nullptr;
    Json p = nullptr;
    if (r.requested) {
        const auto d = methods::Wiener::descriptor();
        method = {{"id", d.id}, {"method_version", d.method_version}};
        p = {
            {"psf", psf_request(r.requested->psf)},
            {"k", r.requested->k},
            {"blend", r.requested->blend},
        };
    }
    return {{"method", method}, {"parameters", p}};
}
Json restoration_fields(const methods::RestorationReport& r) {
    auto fields = restoration_request_fields(r);
    auto warnings = Json::array();
    if (r.inference_warning) {
        warnings.push_back("W_RESTORATION_INFERENCE");
    }
    if (r.after_transform_warning) {
        warnings.push_back("W_PSF_AFTER_TRANSFORM");
    }
    if (r.off_center_warning) {
        warnings.push_back("W_PSF_OFF_CENTER");
    }
    fields.update(Json{
        {"status", methods::status_name(r.status)},
        {"reason", methods::reason_name(r.reason)},
        {"complete", r.complete},
        {"representation", r.requested ? Json("float32") : Json(nullptr)},
        {"psf", psf_fields(r.psf)},
        {"guard", r.guard},
        {"fft_width", r.fft_width},
        {"fft_height", r.fft_height},
        {"padded_mean", r.padded_mean ? Json(*r.padded_mean) : Json(nullptr)},
        {"eligible_samples", r.eligible_samples},
        {"protected_samples", r.protected_samples},
        {"context_samples", r.context_samples},
        {"evaluated_samples", r.evaluated_samples},
        {"corrected_samples", r.corrected_samples},
        {"changed_samples", r.changed_samples},
        {"raw_low_samples", r.raw_low_samples},
        {"raw_high_samples", r.raw_high_samples},
        {"blended_low_samples", r.blended_low_samples},
        {"blended_high_samples", r.blended_high_samples},
        {"raw_fraction", fraction(r.raw_low_samples, r.raw_high_samples, r.evaluated_samples)},
        {
            "blended_fraction",
            fraction(r.blended_low_samples, r.blended_high_samples, r.evaluated_samples),
        },
        {"native_calls", r.native_calls},
        {"native_reserved_bytes", r.native_reserved_bytes},
        {"preparation_charge_peak", r.preparation_charge_peak},
        {"warnings", warnings},
    });
    return fields;
}
} // namespace docenhance::bundle
