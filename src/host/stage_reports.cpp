// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "stage_reports.hpp"

#include "continuous.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <type_traits>
#include <variant>
namespace docenhance::host {
void initialize_stages(const app::ProcessRequest& request, ContinuousReports reports) {
    auto& illumination = reports.illumination.get();
    auto& denoising = reports.denoising.get();
    auto& contrast = reports.contrast.get();
    auto& sharpening = reports.sharpening.get();
    auto& restoration = reports.restoration.get();
    if (const auto* const method = std::get_if<methods::Wiener>(&request.restoration())) {
        restoration.status = methods::RestorationStatus::failed;
        restoration.requested = method->parameters();
        restoration.inference_warning = true;
    }
    std::visit(
        [&](const auto& selected) {
            using M = std::decay_t<decltype(selected)>;
            if constexpr (!std::is_same_v<M, methods::ContrastOff>) {
                contrast.status = methods::ContrastStatus::failed;
                contrast.requested = selected.parameters();
            }
        },
        request.contrast());
    if (const auto* const method = std::get_if<methods::Unsharp>(&request.sharpening())) {
        sharpening.status = methods::SharpenStatus::failed;
        sharpening.requested = method->parameters();
    }
    if (const auto* const method = std::get_if<methods::Surface>(&request.illumination())) {
        illumination.status = methods::IlluminationStatus::failed;
        illumination.requested = method->parameters();
    }
    if (const auto* const method = std::get_if<methods::Morphology>(&request.illumination())) {
        illumination.status = methods::IlluminationStatus::failed;
        illumination.requested = method->parameters();
    }
    if (const auto* const method = std::get_if<methods::Tvl1>(&request.denoising())) {
        denoising.status = methods::DenoiseStatus::failed;
        denoising.requested = method->parameters();
    }
    if (const auto* const method = std::get_if<methods::Nlm>(&request.denoising())) {
        denoising.status = methods::DenoiseStatus::failed;
        denoising.requested = method->parameters();
        denoising.native_h = methods::nlm_native_strength(method->parameters());
    }
}
} // namespace docenhance::host
