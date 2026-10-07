// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <nlohmann/json.hpp>
namespace docenhance::bundle {
using Json = nlohmann::ordered_json;
Json sharpen_request_fields(const methods::SharpenReport& r) {
    Json method = nullptr;
    Json p = nullptr;
    if (r.requested) {
        const auto d = methods::Unsharp::descriptor();
        method = {{"id", d.id}, {"method_version", d.method_version}};
        p = {
            {"sigma", r.requested->sigma},
            {"amount", r.requested->amount},
            {"threshold", r.requested->threshold},
        };
    }
    return {{"method", method}, {"parameters", p}};
}
Json sharpen_fields(const methods::SharpenReport& r) {
    auto fields = sharpen_request_fields(r);
    Json range = nullptr;
    Json fraction = nullptr;
    if (r.pre_clamp) {
        range = {{"low", r.pre_clamp->low}, {"high", r.pre_clamp->high}};
    }
    if (r.evaluated_samples != 0) {
        fraction = static_cast<double>(r.clipped_low_samples + r.clipped_high_samples) /
                   static_cast<double>(r.evaluated_samples);
    }
    auto warnings = Json::array();
    if (r.requested && r.requested->amount != 0) {
        warnings.push_back("W_SHARPENING");
    }
    fields.update(Json{
        {"status", methods::status_name(r.status)},
        {"reason", methods::reason_name(r.reason)},
        {"complete", r.complete},
        {"representation", r.requested ? Json("float64") : Json(nullptr)},
        {"pre_clamp", range},
        {"eligible_samples", r.eligible_samples},
        {"protected_samples", r.protected_samples},
        {"context_samples", r.context_samples},
        {"evaluated_samples", r.evaluated_samples},
        {"corrected_samples", r.corrected_samples},
        {"changed_samples", r.changed_samples},
        {"clipped_low_samples", r.clipped_low_samples},
        {"clipped_high_samples", r.clipped_high_samples},
        {"clipped_fraction", fraction},
        {"preparation_charge_peak", r.preparation_charge_peak},
        {"warnings", warnings},
    });
    return fields;
}
} // namespace docenhance::bundle
