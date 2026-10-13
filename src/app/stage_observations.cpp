// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/stages.hpp"
#include "observations.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::app {
methods::StageObservations initial_stages(const PhotometricPlan& plan) {
    methods::StageObservations reports;
    auto& illumination = reports.illumination;
    auto& denoising = reports.denoising;
    auto& contrast = reports.contrast;
    auto& sharpening = reports.sharpening;
    auto& restoration = reports.restoration;
    if (const auto* const method = std::get_if<methods::Wiener>(&plan.restoration)) {
        restoration.status = methods::RestorationStatus::unresolved;
        restoration.requested = method->parameters();
        restoration.inference_warning = true;
    }
    std::visit(
        [&](const auto& selected) {
            using M = std::decay_t<decltype(selected)>;
            if constexpr (!std::is_same_v<M, methods::ContrastOff>) {
                contrast.status = methods::ContrastStatus::unresolved;
                contrast.requested = selected.parameters();
            }
        },
        plan.contrast);
    if (const auto* const method = std::get_if<methods::Unsharp>(&plan.sharpening)) {
        sharpening.status = methods::SharpenStatus::unresolved;
        sharpening.requested = method->parameters();
    }
    if (const auto* const method = std::get_if<methods::Surface>(&plan.illumination)) {
        illumination.status = methods::IlluminationStatus::unresolved;
        illumination.requested = method->parameters();
    }
    if (const auto* const method = std::get_if<methods::Morphology>(&plan.illumination)) {
        illumination.status = methods::IlluminationStatus::unresolved;
        illumination.requested = method->parameters();
    }
    if (const auto* const method = std::get_if<methods::Tvl1>(&plan.denoising)) {
        denoising.status = methods::DenoiseStatus::unresolved;
        denoising.requested = method->parameters();
    }
    if (const auto* const method = std::get_if<methods::Nlm>(&plan.denoising)) {
        denoising.status = methods::DenoiseStatus::unresolved;
        denoising.requested = method->parameters();
        denoising.native_h = methods::nlm_native_strength(method->parameters());
    }
    return reports;
}
namespace {
bool valid_order(const methods::StageObservations& stages) {
    const auto phase = [](const auto& r, bool lazy) {
        using Status = decltype(r.status);
        return std::array{
            r.progress != methods::StageProgress::not_started,
            (!lazy && !r.complete()) || r.progress == methods::StageProgress::not_started ||
                r.progress == methods::StageProgress::preparing ||
                (r.status == Status::failed && r.progress == methods::StageProgress::prepared),
            r.status == Status::failed && r.progress == methods::StageProgress::evaluating,
            r.status == Status::unresolved,
        };
    };
    bool blocked = false;
    bool upstream_read_failed = false;
    for (const auto& facts : std::array{
             phase(stages.illumination, true),
             phase(stages.denoising, false),
             phase(stages.restoration, false),
             phase(stages.contrast, false),
             phase(stages.sharpening, false),
         }) {
        const auto [started, unusable, read_failed, unresolved] = facts;
        if ((blocked && started) || (upstream_read_failed && started && !unresolved)) {
            return false;
        }
        blocked = blocked || unusable;
        upstream_read_failed = upstream_read_failed || read_failed;
    }
    return true;
}
bool valid_counted_masks(const methods::StageObservations& stages) {
    const auto counted =
        [](const auto& report) -> std::optional<std::pair<std::uint64_t, std::uint64_t>> {
        if (report.progress < methods::StageProgress::prepared) {
            return std::nullopt;
        }
        return std::pair{report.eligible_samples, report.protected_samples};
    };
    std::optional<std::pair<std::uint64_t, std::uint64_t>> expected;
    for (const auto& count : std::array{
             counted(stages.illumination),
             counted(stages.denoising),
             counted(stages.restoration),
             counted(stages.contrast),
             counted(stages.sharpening),
         }) {
        if (count) {
            if (expected && *expected != *count) {
                return false;
            }
            expected = count;
        }
    }
    return true;
}
bool contrast_consumed(const methods::ContrastReport& report) {
    if (report.complete()) {
        return report.status == methods::ContrastStatus::applied ||
               report.reason == methods::ContrastReason::no_effect ||
               report.reason == methods::ContrastReason::insufficient_dynamic_range;
    }
    return report.requested &&
           !std::holds_alternative<methods::GammaParameters>(*report.requested) &&
           (report.progress == methods::StageProgress::prepared ||
            report.progress == methods::StageProgress::evaluating);
}
bool valid_consumption(const methods::StageObservations& stages) {
    const auto consumed = [](const auto& r) {
        return r.progress == methods::StageProgress::prepared ||
               r.progress == methods::StageProgress::evaluating ||
               (r.complete() && (r.status == decltype(r.status)::applied ||
                                 r.reason == decltype(r.reason)::no_effect));
    };
    bool prefix_complete = !stages.illumination.requested || stages.illumination.complete();
    if (consumed(stages.denoising) && !prefix_complete) {
        return false;
    }
    prefix_complete =
        prefix_complete && (!stages.denoising.requested || stages.denoising.complete());
    if (consumed(stages.restoration) && !prefix_complete) {
        return false;
    }
    prefix_complete =
        prefix_complete && (!stages.restoration.requested || stages.restoration.complete());
    // Gamma's fit does not read the prefix; levels and CLAHE do. A flat levels
    // fit is also a complete observing traversal, even though its mapping is identity.
    const auto& c = stages.contrast;
    if (contrast_consumed(c) && !prefix_complete) {
        return false;
    }
    prefix_complete = prefix_complete && (!c.requested || c.complete());
    return !consumed(stages.sharpening) || prefix_complete;
}
} // namespace
bool valid_failure(const ProcessFailure& value, const ProcessRequest& request) {
    if (!value.error.valid_publication()) {
        return false;
    }
    if (!value.stages) {
        return true;
    }
    if (!std::holds_alternative<ContinuousPlan>(request.plan())) {
        return false;
    }
    return valid_order(*value.stages) && valid_partial_stages(*value.stages, request) &&
           valid_counted_masks(*value.stages) && valid_consumption(*value.stages);
}

} // namespace docenhance::app
