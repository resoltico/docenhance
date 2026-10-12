// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/stages.hpp"
#include "observations.hpp"

#include <array>
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
        restoration.status = methods::RestorationStatus::not_started;
        restoration.requested = method->parameters();
        restoration.inference_warning = true;
    }
    std::visit(
        [&](const auto& selected) {
            using M = std::decay_t<decltype(selected)>;
            if constexpr (!std::is_same_v<M, methods::ContrastOff>) {
                contrast.status = methods::ContrastStatus::not_started;
                contrast.requested = selected.parameters();
            }
        },
        plan.contrast);
    if (const auto* const method = std::get_if<methods::Unsharp>(&plan.sharpening)) {
        sharpening.status = methods::SharpenStatus::not_started;
        sharpening.requested = method->parameters();
    }
    if (const auto* const method = std::get_if<methods::Surface>(&plan.illumination)) {
        illumination.status = methods::IlluminationStatus::not_started;
        illumination.requested = method->parameters();
    }
    if (const auto* const method = std::get_if<methods::Morphology>(&plan.illumination)) {
        illumination.status = methods::IlluminationStatus::not_started;
        illumination.requested = method->parameters();
    }
    if (const auto* const method = std::get_if<methods::Tvl1>(&plan.denoising)) {
        denoising.status = methods::DenoiseStatus::not_started;
        denoising.requested = method->parameters();
    }
    if (const auto* const method = std::get_if<methods::Nlm>(&plan.denoising)) {
        denoising.status = methods::DenoiseStatus::not_started;
        denoising.requested = method->parameters();
        denoising.native_h = methods::nlm_native_strength(method->parameters());
    }
    return reports;
}
void failed_stages(methods::StageObservations& reports) {
    auto& report = reports.illumination;
    auto& denoising = reports.denoising;
    auto& contrast = reports.contrast;
    auto& sharpening = reports.sharpening;
    auto& restoration = reports.restoration;
    if (restoration.requested && !restoration.complete &&
        restoration.status != methods::RestorationStatus::not_started) {
        restoration.status = methods::RestorationStatus::failed;
        restoration.reason = methods::RestorationReason::processing_failure;
    }
    if (report.requested && !report.complete &&
        report.status != methods::IlluminationStatus::not_started) {
        report.status = methods::IlluminationStatus::failed;
        if (report.reason == methods::IlluminationReason::none ||
            report.reason == methods::IlluminationReason::no_effect) {
            report.reason = methods::IlluminationReason::processing_failure;
        }
    }
    if (denoising.requested && !denoising.complete &&
        denoising.status != methods::DenoiseStatus::not_started) {
        denoising.status = methods::DenoiseStatus::failed;
        denoising.reason = methods::DenoiseReason::processing_failure;
    }
    if (sharpening.requested && !sharpening.complete &&
        sharpening.status != methods::SharpenStatus::not_started) {
        sharpening.status = methods::SharpenStatus::failed;
        sharpening.reason = methods::SharpenReason::processing_failure;
    }
    if (contrast.requested && !contrast.complete &&
        contrast.status != methods::ContrastStatus::not_started) {
        contrast.status = methods::ContrastStatus::failed;
        contrast.reason = methods::ContrastReason::processing_failure;
    }
}
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
    const auto& stages = *value.stages;
    const auto phase = [](const auto& report) {
        using Status = decltype(report.status);
        return std::pair{
            report.requested.has_value() && !report.complete,
            report.complete ||
                (report.requested.has_value() && report.status != Status::not_started),
        };
    };
    // A selected incomplete predecessor forbids later work in the fixed photometric sequence.
    bool blocked = false;
    for (const auto& [incomplete, started] : std::array{
             phase(stages.illumination),
             phase(stages.denoising),
             phase(stages.restoration),
             phase(stages.contrast),
             phase(stages.sharpening),
         }) {
        if (blocked && started) {
            return false;
        }
        blocked = blocked || incomplete;
    }
    return valid_partial_stages(stages, request);
}
} // namespace docenhance::app
