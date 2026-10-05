// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "read_fields.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <variant>
namespace docenhance::bundle {
core::Result<methods::DenoisingReport> record_denoising(const RecordJson& value) {
    methods::DenoisingReport r;
    const auto& p = record_field(value, "parameters");
    methods::Denoising selected = methods::DenoisingOff{};
    if (!p.is_null()) {
        auto method = methods::Nlm::create({
            .h = record_number(record_field(p, "h")),
            .patch =
                static_cast<std::uint32_t>(record_integer(record_field(p, "patch"), UINT32_MAX)),
            .search =
                static_cast<std::uint32_t>(record_integer(record_field(p, "search"), UINT32_MAX)),
            .blend = record_number(record_field(p, "blend")),
        });
        if (!method) {
            return core::failure(core::ErrorCode::input, "Invalid recorded NLM parameters");
        }
        r.requested = method->parameters();
        selected = *method;
    }
    for (const auto status : std::array{
             methods::DenoiseStatus::disabled,
             methods::DenoiseStatus::no_change,
             methods::DenoiseStatus::applied,
             methods::DenoiseStatus::failed,
         }) {
        if (methods::status_name(status) == record_text(record_field(value, "status"))) {
            r.status = status;
        }
    }
    for (const auto reason : std::array{
             methods::DenoiseReason::none,
             methods::DenoiseReason::zero_blend,
             methods::DenoiseReason::no_eligible_samples,
             methods::DenoiseReason::no_effect,
             methods::DenoiseReason::processing_failure,
         }) {
        if (methods::reason_name(reason) == record_text(record_field(value, "reason"))) {
            r.reason = reason;
        }
    }
    r.complete = record_boolean(record_field(value, "complete"));
    r.native_h = record_number(record_field(value, "native_h"));
    r.eligible_samples = record_integer(record_field(value, "eligible_samples"), UINT64_MAX);
    r.protected_samples = record_integer(record_field(value, "protected_samples"), UINT64_MAX);
    r.evaluated_samples = record_integer(record_field(value, "evaluated_samples"), UINT64_MAX);
    r.corrected_samples = record_integer(record_field(value, "corrected_samples"), UINT64_MAX);
    r.changed_samples = record_integer(record_field(value, "changed_samples"), UINT64_MAX);
    r.native_calls = record_integer(record_field(value, "native_calls"), UINT64_MAX);
    r.native_reserved_peak =
        record_integer(record_field(value, "native_reserved_peak"), UINT64_MAX);
    constexpr std::uint64_t preparation_limit = std::uint64_t{1024} * 1024 * 1024;
    r.preparation_charge_peak =
        record_integer(record_field(value, "preparation_charge_peak"), preparation_limit);
    if (!methods::valid_denoising(r, selected) || RecordJson(denoising_fields(r)) != value) {
        return core::failure(core::ErrorCode::input, "Invalid denoising execution claims");
    }
    return r;
}
namespace {
core::Result<void> observations_agree(const methods::DenoisingReport& r, const Operation& operation,
                                      const methods::IlluminationReport& light,
                                      image::RasterShape shape) {
    const auto pixels = std::uint64_t{shape.width} * shape.height;
    bool agrees = false;
    if (std::holds_alternative<methods::Binarization>(operation)) {
        agrees = !r.requested && r.eligible_samples == 0 && r.protected_samples == 0;
    } else {
        agrees = r.protected_samples <= pixels &&
                 r.eligible_samples == pixels - r.protected_samples &&
                 r.eligible_samples == light.eligible_samples &&
                 r.protected_samples == light.protected_samples &&
                 methods::valid_denoising_extent(r, {.width = shape.width, .height = shape.height});
    }
    return agrees ? core::Result<void>{}
                  : core::failure(core::ErrorCode::input, "Inconsistent denoising observations");
}
} // namespace
core::Result<void> validate_denoising_claims(const RecordJson& document, DeclaredBundle& d,
                                             const Operation& operation,
                                             const methods::IlluminationReport& light) {
    auto denoising =
        record_denoising(record_field(record_field(document, "execution"), "denoising"));
    if (!denoising) {
        return core::failure(core::ErrorCode::input, "Inconsistent denoising claims");
    }
    d.denoising = *denoising;
    const auto& denoise_request = record_field(record_field(document, "request"), "denoising");
    if (RecordJson(denoising_request_fields(*denoising)) != denoise_request) {
        return core::failure(core::ErrorCode::input, "Inconsistent denoising claims");
    }
    return observations_agree(*denoising, operation, light, d.output.shape);
}

} // namespace docenhance::bundle
