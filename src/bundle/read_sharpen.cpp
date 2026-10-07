// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "read_fields.hpp"

#include <array>
#include <expected>
#include <variant>
namespace docenhance::bundle {
core::Result<void> validate_sharpen_claims(const RecordJson& document, DeclaredBundle& d,
                                           const Operation& operation,
                                           const methods::IlluminationReport& light) {
    const auto& v = record_field(record_field(document, "execution"), "sharpening");
    methods::SharpenReport r;
    methods::Sharpening method = methods::SharpenOff{};
    const auto& p = record_field(v, "parameters");
    if (!p.is_null()) {
        auto selected = methods::Unsharp::create({
            .sigma = record_number(record_field(p, "sigma")),
            .amount = record_number(record_field(p, "amount")),
            .threshold = record_number(record_field(p, "threshold")),
        });
        if (!selected) {
            return core::failure(core::ErrorCode::input, "Invalid recorded sharpening parameters");
        }
        method = *selected;
        r.requested = selected->parameters();
        r.status = methods::SharpenStatus::failed;
    }
    for (const auto status : std::array{
             methods::SharpenStatus::disabled,
             methods::SharpenStatus::no_change,
             methods::SharpenStatus::applied,
             methods::SharpenStatus::failed,
         }) {
        if (methods::status_name(status) == record_text(record_field(v, "status"))) {
            r.status = status;
        }
    }
    for (const auto reason : std::array{
             methods::SharpenReason::none,
             methods::SharpenReason::zero_amount,
             methods::SharpenReason::no_eligible_samples,
             methods::SharpenReason::no_effect,
             methods::SharpenReason::processing_failure,
         }) {
        if (methods::reason_name(reason) == record_text(record_field(v, "reason"))) {
            r.reason = reason;
        }
    }
    r.complete = record_boolean(record_field(v, "complete"));
    const auto& range = record_field(v, "pre_clamp");
    if (!range.is_null()) {
        r.pre_clamp = methods::ExcursionRange{
            .low = record_number(record_field(range, "low")),
            .high = record_number(record_field(range, "high")),
        };
    }
    constexpr auto bound = image::source_pixels_max;
    r.eligible_samples = record_integer(record_field(v, "eligible_samples"), bound);
    r.protected_samples = record_integer(record_field(v, "protected_samples"), bound);
    r.context_samples = record_integer(record_field(v, "context_samples"), bound);
    r.evaluated_samples = record_integer(record_field(v, "evaluated_samples"), bound);
    r.corrected_samples = record_integer(record_field(v, "corrected_samples"), bound);
    r.changed_samples = record_integer(record_field(v, "changed_samples"), bound);
    r.clipped_low_samples = record_integer(record_field(v, "clipped_low_samples"), bound);
    r.clipped_high_samples = record_integer(record_field(v, "clipped_high_samples"), bound);
    r.preparation_charge_peak = record_integer(record_field(v, "preparation_charge_peak"),
                                               core::continuous_processing_budget);
    if (!methods::valid_sharpen(r, method) || RecordJson(sharpen_fields(r)) != v ||
        RecordJson(sharpen_request_fields(r)) !=
            record_field(record_field(document, "request"), "sharpening")) {
        return core::failure(core::ErrorCode::input, "Invalid recorded sharpening observations");
    }
    if (std::holds_alternative<methods::Binarization>(operation)) {
        if (r.requested || r.eligible_samples != 0 || r.protected_samples != 0) {
            return core::failure(core::ErrorCode::input, "Binary record cannot claim sharpening");
        }
    } else if (!methods::valid_sharpen_extent(
                   r, {.width = d.output.shape.width, .height = d.output.shape.height}) ||
               r.eligible_samples != light.eligible_samples ||
               r.protected_samples != light.protected_samples) {
        return core::failure(core::ErrorCode::input,
                             "Inconsistent sharpening source extent or charge");
    }
    d.sharpening = r;
    return {};
}
} // namespace docenhance::bundle
