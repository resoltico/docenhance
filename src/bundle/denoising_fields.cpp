// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"

#include <nlohmann/json.hpp>
namespace docenhance::bundle {
namespace {
nlohmann::ordered_json parameters_fields(const methods::NlmParameters& p) {
    return {{"h", p.h}, {"patch", p.patch}, {"search", p.search}, {"blend", p.blend}};
}
} // namespace
nlohmann::ordered_json denoising_request_fields(const methods::DenoisingReport& r) {
    using Json = nlohmann::ordered_json;
    return {{"method", r.requested
                           ? Json{{"id", methods::Nlm::descriptor().id},
                                  {"method_version", methods::Nlm::descriptor().method_version},}
                           : Json(nullptr),},
            {"parameters", r.requested ? parameters_fields(*r.requested) : Json(nullptr)},};
}
nlohmann::ordered_json denoising_fields(const methods::DenoisingReport& r) {
    auto fields = denoising_request_fields(r);
    fields.update(nlohmann::ordered_json{
        {"status", methods::status_name(r.status)},
        {"reason", methods::reason_name(r.reason)},
        {"complete", r.complete},
        {"analysis_depth", image::word_bits},
        {"distance", "L1"},
        {"native_h", r.native_h},
        {"tile_width", methods::nlm_tile_width},
        {"border", "REFLECT_101"},
        {"eligible_samples", r.eligible_samples},
        {"protected_samples", r.protected_samples},
        {"evaluated_samples", r.evaluated_samples},
        {"corrected_samples", r.corrected_samples},
        {"changed_samples", r.changed_samples},
        {"native_calls", r.native_calls},
        {"native_reserved_peak", r.native_reserved_peak},
        {"preparation_charge_peak", r.preparation_charge_peak},
    });
    return fields;
}
} // namespace docenhance::bundle
