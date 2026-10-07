// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"
#include "docenhance/methods/contrast.hpp"

#include <nlohmann/json.hpp>
#include <type_traits>
#include <variant>
namespace docenhance::bundle {
namespace {
using Json = nlohmann::ordered_json;
Json parameters(const methods::LevelsParameters& p) {
    return {
        {"low", p.low},
        {"high", p.high},
        {"blend", p.blend},
    };
}
Json parameters(const methods::GammaParameters& p) {
    return {
        {"gamma", p.gamma},
        {"blend", p.blend},
    };
}
Json parameters(const methods::ClaheParameters& p) {
    return {
        {"grid_columns", p.grid_columns},
        {"grid_rows", p.grid_rows},
        {"clip", p.clip},
        {"blend", p.blend},
    };
}
} // namespace
Json contrast_request_fields(const methods::ContrastReport& r) {
    Json method = nullptr;
    Json values = nullptr;
    if (r.requested) {
        const auto d = std::visit(
            [](const auto& p) {
                using P = std::decay_t<decltype(p)>;
                if constexpr (std::is_same_v<P, methods::LevelsParameters>) {
                    return methods::Levels::descriptor();
                } else if constexpr (std::is_same_v<P, methods::GammaParameters>) {
                    return methods::Gamma::descriptor();
                } else {
                    return methods::Clahe::descriptor();
                }
            },
            *r.requested);
        method = {{"id", d.id}, {"method_version", d.method_version}};
        values = std::visit([](const auto& p) { return parameters(p); }, *r.requested);
    }
    return {{"method", method}, {"parameters", values}};
}
Json contrast_fields(const methods::ContrastReport& r) {
    auto fields = contrast_request_fields(r);
    Json range = nullptr;
    Json low = nullptr;
    Json high = nullptr;
    if (r.levels) {
        range = {{"low", r.levels->low}, {"high", r.levels->high}};
    }
    if (r.levels && r.evaluated_samples != 0) {
        low = static_cast<double>(r.clipped_low_samples) / static_cast<double>(r.evaluated_samples);
        high =
            static_cast<double>(r.clipped_high_samples) / static_cast<double>(r.evaluated_samples);
    }
    fields.update(Json{
        {"status", methods::status_name(r.status)},
        {"reason", methods::reason_name(r.reason)},
        {"complete", r.complete},
        {"representation", r.requested ? Json("float64") : Json(nullptr)},
        {"levels", range},
        {"eligible_samples", r.eligible_samples},
        {"protected_samples", r.protected_samples},
        {"measured_samples", r.measured_samples},
        {"identity_tiles", r.identity_tiles},
        {"evaluated_samples", r.evaluated_samples},
        {"corrected_samples", r.corrected_samples},
        {"changed_samples", r.changed_samples},
        {"clipped_low_samples", r.clipped_low_samples},
        {"clipped_high_samples", r.clipped_high_samples},
        {"clipped_low_fraction", low},
        {"clipped_high_fraction", high},
        {"preparation_charge_peak", r.preparation_charge_peak},
    });
    return fields;
}
} // namespace docenhance::bundle
