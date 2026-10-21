// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/record/method_requests.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/request.hpp"
#include "validation.hpp"

#include <cstdint>
#include <optional>
#include <variant>
namespace docenhance::record {
namespace {
struct ObservationContext {
    image::Extent extent;
    std::uint64_t protected_samples{};
};
bool consistent(const SurfaceRequest& request, const methods::SurfaceFacts& facts,
                image::Extent extent) {
    const auto& p = request.parameters;
    return methods::validate(facts,
                             {
                                 .mode = request.selector,
                                 .strength = p.strength,
                                 .max_gain = p.max_gain,
                                 .target = p.target,
                                 .cell = p.cell,
                                 .quantile = p.quantile,
                                 .smooth = p.smooth,
                             },
                             extent);
}
bool consistent(const MorphologyRequest& request, const methods::MorphologyFacts& facts,
                image::Extent extent) {
    return methods::validate(facts, request.parameters, extent);
}
bool consistent(const NlmRequest& request, const methods::NlmFacts& facts,
                image::Extent /*extent*/) {
    return methods::validate(facts, request.parameters);
}
bool consistent(const Tvl1Request& request, const methods::Tvl1Facts& facts,
                image::Extent /*extent*/) {
    return methods::validate(facts, request.parameters);
}
bool consistent(const WienerRequest& request, const methods::WienerFacts& facts,
                image::Extent extent) {
    return methods::validate(facts, request.parameters, extent);
}
bool consistent(const LevelsRequest& request, const methods::LevelsFacts& facts,
                image::Extent /*extent*/) {
    return methods::validate(facts, request.parameters);
}
bool consistent(const GammaRequest& request, const methods::GammaFacts& facts,
                image::Extent /*extent*/) {
    return methods::validate(facts, request.parameters);
}
bool consistent(const ClaheRequest& request, const methods::ClaheFacts& facts,
                image::Extent /*extent*/) {
    return methods::validate(facts, request.parameters);
}
bool consistent(const UnsharpRequest& request, const methods::UnsharpFacts& facts,
                image::Extent /*extent*/) {
    return methods::validate(facts, request.parameters);
}
bool consistent(const OtsuRequest& /*request*/, const methods::OtsuFacts& facts,
                image::Extent /*extent*/) {
    return methods::validate(facts);
}
bool consistent(const SauvolaRequest& request, const methods::SauvolaFacts& facts,
                image::Extent /*extent*/) {
    return methods::validate(facts, request.parameters);
}
bool consistent(const FixedThresholdRequest& request, const methods::FixedFacts& facts,
                image::Extent /*extent*/) {
    return methods::validate(facts, request.parameters);
}
template <typename Request, typename Facts>
bool matched(const Request& request, const Facts& facts, ObservationContext context) {
    if constexpr (requires { consistent(request, facts, context.extent); }) {
        if (!consistent(request, facts, context.extent)) {
            return false;
        }
        const auto pixels = std::uint64_t{context.extent.width} * context.extent.height;
        if constexpr (requires { facts.counts; }) {
            return methods::counts_match(facts.counts, pixels) &&
                   facts.counts.protected_samples == context.protected_samples;
        } else {
            return facts.black_samples + facts.white_samples == pixels;
        }
    } else {
        return false;
    }
}
template <typename Request, typename Facts>
bool pair_agrees(const std::optional<Request>& request, const std::optional<Facts>& facts,
                 ObservationContext context) {
    if (request.has_value() != facts.has_value()) {
        return false;
    }
    if (!request) {
        return true;
    }
    if constexpr (requires { request->valueless_by_exception(); }) {
        if (request->valueless_by_exception() || facts->valueless_by_exception()) {
            return false;
        }
        return std::visit([&](const auto& selection,
                              const auto& report) { return matched(selection, report, context); },
                          *request, *facts);
    } else {
        return matched(*request, *facts, context);
    }
}
} // namespace
bool execution_agrees(const RunRecord& value) {
    ObservationContext context{
        .extent = {.width = value.output.width, .height = value.output.height},
    };
    for (const auto& asset : value.assets) {
        if (const auto* const mask = std::get_if<ProtectionAsset>(&asset)) {
            context.protected_samples = mask->protected_samples;
        }
    }
    const auto& request = value.request;
    const auto& reports = value.execution;
    return pair_agrees(request.illumination, reports.illumination, context) &&
           pair_agrees(request.denoising, reports.denoising, context) &&
           pair_agrees(request.restoration, reports.restoration, context) &&
           pair_agrees(request.contrast, reports.contrast, context) &&
           pair_agrees(request.sharpening, reports.sharpening, context) &&
           pair_agrees(request.binarization, reports.binarization, context);
}
} // namespace docenhance::record
