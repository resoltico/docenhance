// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "read_fields.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <type_traits>
#include <variant>
namespace docenhance::bundle {
namespace {
core::Result<methods::Denoising> recorded_method(const RecordJson& value) {
    const auto& p = record_field(value, "parameters");

    if (!p.is_null()) {
        if (record_text(record_field(record_field(value, "method"), "id")) ==
            methods::Tvl1::descriptor().id) {
            auto method = methods::Tvl1::create({
                .lambda = record_number(record_field(p, "lambda")),
                .iterations = static_cast<std::uint32_t>(
                    record_integer(record_field(p, "iterations"), UINT32_MAX)),
                .tolerance = record_number(record_field(p, "tolerance")),
                .blend = record_number(record_field(p, "blend")),
            });
            if (!method) {
                return core::failure(core::ErrorCode::input, "Invalid recorded TV-L1 parameters");
            }
            return methods::Denoising{*method};
        }
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
        return methods::Denoising{*method};
    }
    return methods::Denoising{methods::DenoisingOff{}};
}
std::optional<methods::Tvl1Report> recorded_tvl1(const RecordJson& tv) {

    if (!tv.is_null()) {
        methods::Tvl1Report details;
        details.iterations =
            static_cast<std::uint32_t>(record_integer(record_field(tv, "iterations"), UINT32_MAX));
        details.passing_checkpoints = static_cast<std::uint32_t>(
            record_integer(record_field(tv, "passing_checkpoints"), UINT32_MAX));
        details.primal_update = record_number(record_field(tv, "primal_update"));
        details.dual_update = record_number(record_field(tv, "dual_update"));
        details.field_bytes = record_integer(record_field(tv, "field_bytes"), UINT64_MAX);
        if (!record_field(tv, "stop").is_null()) {
            const auto stop = record_text(record_field(tv, "stop"));
            if (stop == "tolerance_met") {
                details.stop = methods::Tvl1Stop::tolerance_met;
            } else if (stop == "iteration_limit") {
                details.stop = methods::Tvl1Stop::iteration_limit;
            }
        }
        if (!record_field(tv, "objective_start").is_null()) {
            details.objective_start = record_number(record_field(tv, "objective_start"));
        }
        if (!record_field(tv, "objective_end").is_null()) {
            details.objective_end = record_number(record_field(tv, "objective_end"));
        }
        return details;
    }
    return std::nullopt;
}
} // namespace
core::Result<methods::DenoisingReport> record_denoising(const RecordJson& value) {
    methods::DenoisingReport r;
    auto selected_result = recorded_method(value);
    if (!selected_result) {
        return std::unexpected(selected_result.error());
    }
    const auto selected = *selected_result;
    std::visit(
        [&](const auto& method) {
            using Method = std::decay_t<decltype(method)>;
            if constexpr (!std::is_same_v<Method, methods::DenoisingOff>) {
                r.requested = method.parameters();
            }
        },
        selected);
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
    r.tvl1 = recorded_tvl1(record_field(value, "tvl1"));
    r.native_h = record_number(record_field(value, "native_h"));
    r.eligible_samples = record_integer(record_field(value, "eligible_samples"), UINT64_MAX);
    r.protected_samples = record_integer(record_field(value, "protected_samples"), UINT64_MAX);
    r.evaluated_samples = record_integer(record_field(value, "evaluated_samples"), UINT64_MAX);
    r.corrected_samples = record_integer(record_field(value, "corrected_samples"), UINT64_MAX);
    r.changed_samples = record_integer(record_field(value, "changed_samples"), UINT64_MAX);
    r.native_calls = record_integer(record_field(value, "native_calls"), UINT64_MAX);
    r.native_reserved_peak =
        record_integer(record_field(value, "native_reserved_peak"), UINT64_MAX);
    r.preparation_charge_peak = record_integer(record_field(value, "preparation_charge_peak"),
                                               core::continuous_processing_budget);
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
