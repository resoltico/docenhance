// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/fields.hpp"
#include "docenhance/methods/denoising.hpp"

#include <nlohmann/json.hpp>
namespace docenhance::bundle {
nlohmann::ordered_json tvl1_fields(const methods::Tvl1Report& report) {
    using Json = nlohmann::ordered_json;
    Json stop = nullptr;
    if (report.stop) {
        stop =
            *report.stop == methods::Tvl1Stop::tolerance_met ? "tolerance_met" : "iteration_limit";
    }
    return {
        {"iterations", report.iterations},
        {"stop", stop},
        {"passing_checkpoints", report.passing_checkpoints},
        {"primal_update", report.primal_update},
        {"dual_update", report.dual_update},
        {"objective_start", report.objective_start ? Json(*report.objective_start) : Json(nullptr)},
        {"objective_end", report.objective_end ? Json(*report.objective_end) : Json(nullptr)},
        {"field_bytes", report.field_bytes},
    };
}
} // namespace docenhance::bundle
