// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "illumination.hpp"

#include "docenhance/core/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"

#include <expected>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::report {
namespace {
core::Result<std::string> surface_text(const methods::SurfaceFacts& facts,
                                       const record::Json& encoded) {
    std::string text = "I01 grid: cells=" + std::to_string(facts.cells) +
                       "; measured=" + std::to_string(facts.measured_cells) +
                       "; dark=" + std::to_string(facts.dark_cells) + "\n";
    if (facts.resolved_cell) {
        text += "I01 resolved cell: " + std::to_string(*facts.resolved_cell) + "\n";
    }
    if (facts.background_reference) {
        text += "I01 background reference: " + encoded.at("background_reference").dump() + "\n";
    }
    if (facts.coverage) {
        text += "I01 coverage: " + encoded.at("coverage").dump() + "\n";
    }
    if (facts.solver) {
        text += "I01 solver: iterations=" + std::to_string(facts.solver->iterations) +
                "; residual=" + encoded.at("solver").at("residual").dump() +
                "; tolerance=" + encoded.at("solver").at("tolerance").dump() + "\n";
    }
    if (facts.measurements) {
        const auto& analysis = *facts.measurements;
        text += "I01 analysis: stride=" + std::to_string(analysis.stride) +
                "; samples=" + std::to_string(analysis.count) +
                "; fallback=" + encoded.at("measurements").at("fallback").dump() +
                "; target=" + encoded.at("measurements").at("target").dump() + "\n";
        text += "I01 background quantiles: q10=" +
                encoded.at("measurements").at("background_q10").dump() +
                "; q50=" + encoded.at("measurements").at("background_q50").dump() +
                "; q90=" + encoded.at("measurements").at("background_q90").dump() +
                "; luminance q90=" + encoded.at("measurements").at("luminance_q90").dump() + "\n";
        text += "I01 measurements: variation=" + encoded.at("measurements").at("variation").dump() +
                "; paper fraction=" + encoded.at("measurements").at("paper_fraction").dump() +
                "; dark fraction=" + encoded.at("measurements").at("dark_fraction").dump() + "\n";
    }
    // Optional predicates retain null for a predicate that was not evaluated.
    text += "I01 automatic predicates: " + encoded.at("auto_predicates").dump() + "\n";
    return text;
}
} // namespace
core::Result<std::string>
illumination_text(const std::optional<methods::IlluminationFacts>& facts) {
    if (!facts) {
        return std::string{};
    }
    auto encoded = record::Codec::encode(*facts);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    return std::visit(
        [&](const auto& actual) -> core::Result<std::string> {
            using Facts = std::decay_t<decltype(actual)>;
            std::string text = "Illumination " + std::string(core::WireTag<Facts>::value) + ": " +
                               encoded->at("disposition").get<std::string>() + " (" +
                               encoded->at("reason").get<std::string>() +
                               "); counts=" + encoded->at("counts").dump() + "\n";
            if constexpr (std::is_same_v<Facts, methods::SurfaceFacts>) {
                auto surface = surface_text(actual, *encoded);
                if (!surface) {
                    return std::unexpected(std::move(surface.error()));
                }
                text += *surface;
            } else {
                text += "I02 field: radius=" + std::to_string(actual.resolved_radius) +
                        "; sigma=" + encoded->at("sigma").dump() +
                        "; Gaussian radius=" + std::to_string(actual.gaussian_radius) + "\n";
                if (actual.analysis) {
                    const auto& analysis = *actual.analysis;
                    text += "I02 analysis: stride=" + std::to_string(analysis.stride) +
                            "; samples=" + std::to_string(analysis.count) +
                            "; fallback=" + encoded->at("analysis").at("fallback").dump() +
                            "; fill=" + encoded->at("analysis").at("analysis_fill").dump() +
                            "; target=" + encoded->at("analysis").at("target").dump() + "\n";
                    text += "I02 background: q10=" +
                            encoded->at("analysis").at("background_q10").dump() +
                            "; q50=" + encoded->at("analysis").at("background_q50").dump() +
                            "; q90=" + encoded->at("analysis").at("background_q90").dump() +
                            "; min=" + encoded->at("analysis").at("background_min").dump() +
                            "; max=" + encoded->at("analysis").at("background_max").dump() + "\n";
                }
            }
            text += "Illumination gains: min=" + encoded->at("min_gain").dump() +
                    "; max=" + encoded->at("max_gain").dump() +
                    "; capped=" + std::to_string(actual.gain_capped_samples) +
                    "; saturated=" + std::to_string(actual.saturated_samples) + "\n";
            return text;
        },
        *facts);
}
} // namespace docenhance::report
