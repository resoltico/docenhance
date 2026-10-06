// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/illumination.hpp"
#include "read_fields.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <variant>
namespace docenhance::bundle {
namespace {
core::Result<methods::Contrast> selected_method(const RecordJson& value) {
    const auto& p = record_field(value, "parameters");
    if (p.is_null()) {
        return methods::ContrastOff{};
    }
    const auto id = record_text(record_field(record_field(value, "method"), "id"));
    if (id == methods::Levels::descriptor().id) {
        auto method = methods::Levels::create({
            .low = record_number(record_field(p, "low")),
            .high = record_number(record_field(p, "high")),
            .blend = record_number(record_field(p, "blend")),
        });
        if (method) {
            return methods::Contrast{*method};
        }
    } else if (id == methods::Gamma::descriptor().id) {
        auto method = methods::Gamma::create({
            .gamma = record_number(record_field(p, "gamma")),
            .blend = record_number(record_field(p, "blend")),
        });
        if (method) {
            return methods::Contrast{*method};
        }
    }
    return core::failure(core::ErrorCode::input, "Invalid recorded contrast parameters");
}
core::Result<methods::ContrastReport> observations(const RecordJson& v,
                                                   const methods::Contrast& method) {
    methods::ContrastReport r;
    std::visit(
        [&](const auto& m) {
            using M = std::decay_t<decltype(m)>;
            if constexpr (!std::is_same_v<M, methods::ContrastOff>) {
                r.requested = m.parameters();
            }
        },
        method);
    for (const auto s : std::array{
             methods::ContrastStatus::disabled,
             methods::ContrastStatus::no_change,
             methods::ContrastStatus::applied,
             methods::ContrastStatus::failed,
         }) {
        if (methods::status_name(s) == record_text(record_field(v, "status"))) {
            r.status = s;
        }
    }
    for (const auto reason : std::array{
             methods::ContrastReason::none,
             methods::ContrastReason::zero_blend,
             methods::ContrastReason::no_eligible_samples,
             methods::ContrastReason::identity_gamma,
             methods::ContrastReason::insufficient_dynamic_range,
             methods::ContrastReason::no_effect,
             methods::ContrastReason::processing_failure,
         }) {
        if (methods::reason_name(reason) == record_text(record_field(v, "reason"))) {
            r.reason = reason;
        }
    }
    r.complete = record_boolean(record_field(v, "complete"));
    const auto& range = record_field(v, "levels");
    if (!range.is_null()) {
        r.levels = methods::LevelsRange{
            .low = record_number(record_field(range, "low")),
            .high = record_number(record_field(range, "high")),
        };
    }
    r.eligible_samples =
        record_integer(record_field(v, "eligible_samples"), image::source_pixels_max);
    r.protected_samples =
        record_integer(record_field(v, "protected_samples"), image::source_pixels_max);
    r.measured_samples =
        record_integer(record_field(v, "measured_samples"), image::source_pixels_max);
    r.evaluated_samples =
        record_integer(record_field(v, "evaluated_samples"), image::source_pixels_max);
    r.corrected_samples =
        record_integer(record_field(v, "corrected_samples"), image::source_pixels_max);
    r.changed_samples =
        record_integer(record_field(v, "changed_samples"), image::source_pixels_max);
    r.clipped_low_samples =
        record_integer(record_field(v, "clipped_low_samples"), image::source_pixels_max);
    r.clipped_high_samples =
        record_integer(record_field(v, "clipped_high_samples"), image::source_pixels_max);
    r.preparation_charge_peak = record_integer(record_field(v, "preparation_charge_peak"),
                                               core::continuous_processing_budget);
    if (!methods::valid_contrast(r, method) || RecordJson(contrast_fields(r)) != v) {
        return core::failure(core::ErrorCode::input, "Invalid recorded contrast observations");
    }
    return r;
}
} // namespace
core::Result<void> validate_contrast_claims(const RecordJson& document, DeclaredBundle& d,
                                            const Operation& operation,
                                            const methods::IlluminationReport& light) {
    const auto& v = record_field(record_field(document, "execution"), "contrast");
    auto method = selected_method(v);
    if (!method) {
        return std::unexpected(method.error());
    }
    auto report = observations(v, *method);
    if (!report) {
        return std::unexpected(report.error());
    }
    if (RecordJson(contrast_request_fields(*report)) !=
        record_field(record_field(document, "request"), "contrast")) {
        return core::failure(core::ErrorCode::input, "Inconsistent contrast request");
    }
    const auto pixels = std::uint64_t{d.output.shape.width} * d.output.shape.height;
    const bool binary = std::holds_alternative<methods::Binarization>(operation);
    if (binary ? (report->requested.has_value() || report->eligible_samples != 0 ||
                  report->protected_samples != 0)
               : (report->protected_samples > pixels ||
                  report->eligible_samples != pixels - report->protected_samples ||
                  report->eligible_samples != light.eligible_samples ||
                  report->protected_samples != light.protected_samples)) {
        return core::failure(core::ErrorCode::input, "Inconsistent contrast source extent");
    }
    d.contrast = *report;
    return {};
}
} // namespace docenhance::bundle
