// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"

#include <nlohmann/json.hpp>
#include <variant>
namespace docenhance::bundle {
namespace {
nlohmann::ordered_json parameters_fields(const methods::NlmParameters& p) {
    return {
        {"h", p.h},
        {"patch", p.patch},
        {"search", p.search},
        {"blend", p.blend},
    };
}
nlohmann::ordered_json parameters_fields(const methods::Tvl1Parameters& p) {
    return {
        {"lambda", p.lambda},
        {"iterations", p.iterations},
        {"tolerance", p.tolerance},
        {"blend", p.blend},
    };
}
} // namespace
nlohmann::ordered_json denoising_request_fields(const methods::DenoisingReport& r) {
    using Json = nlohmann::ordered_json;
    Json method = nullptr;
    Json parameters = nullptr;
    if (r.requested) {
        const auto descriptor = std::holds_alternative<methods::Tvl1Parameters>(*r.requested)
                                    ? methods::Tvl1::descriptor()
                                    : methods::Nlm::descriptor();
        method = {{"id", descriptor.id}, {"method_version", descriptor.method_version}};
        parameters = std::visit([](const auto& p) { return parameters_fields(p); }, *r.requested);
    }
    return {{"method", method}, {"parameters", parameters}};
}
nlohmann::ordered_json denoising_fields(const methods::DenoisingReport& r) {
    using Json = nlohmann::ordered_json;
    auto fields = denoising_request_fields(r);
    const bool tv = r.requested && std::holds_alternative<methods::Tvl1Parameters>(*r.requested);
    const bool nlm = r.requested && !tv;
    Json warnings = Json::array();
    if (r.tvl1 && r.tvl1->stop == methods::Tvl1Stop::iteration_limit) {
        warnings.push_back("W_TV_ITERATION_LIMIT");
    }

    Json precision = nullptr;
    Json representation = nullptr;
    Json border = nullptr;
    if (tv) {
        constexpr unsigned floating_bits = 64;
        precision = floating_bits;
        representation = "float64";
        border = "forward_zero_terminal";
    } else if (nlm) {
        precision = image::word_bits;
        representation = "uint16";
        border = "REFLECT_101";
    }
    fields.update(nlohmann::ordered_json{
        {"status", methods::status_name(r.status)},
        {"reason", methods::reason_name(r.reason)},
        {"complete", r.complete},
        {"analysis_depth", precision},
        {"distance", r.requested ? Json("L1") : Json(nullptr)},
        {"representation", representation},
        {"tvl1", r.tvl1 ? tvl1_fields(*r.tvl1) : Json(nullptr)},
        {"warnings", warnings},
        {"native_h", r.native_h},
        {"tile_width", nlm ? methods::nlm_tile_width : 0},
        {"border", border},
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
