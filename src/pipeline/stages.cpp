// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/clahe.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/gamma.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/levels.hpp"
#include "docenhance/methods/morphology_working.hpp"
#include "docenhance/methods/nlm_working.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_working.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/surface_working.hpp"
#include "docenhance/methods/tvl1_working.hpp"
#include "docenhance/methods/unsharp.hpp"
#include "docenhance/methods/warnings.hpp"
#include "docenhance/methods/working.hpp"
#include "docenhance/pipeline/render.hpp"
#include "failure_context.hpp"
#include "frame.hpp"

#include <expected>
#include <type_traits>
#include <utility>
#include <variant>

namespace docenhance::pipeline {
namespace {
template <typename Variant, typename Slot>
core::Result<void> apply_family(const Variant& selected, methods::Working working, Slot& slot,
                                methods::FailureWarningContext& context) {
    return std::visit(
        [&](const auto& method) -> core::Result<void> {
            using Method = std::remove_cvref_t<decltype(method)>;
            if constexpr (std::is_same_v<Method, methods::IlluminationOff> ||
                          std::is_same_v<Method, methods::DenoisingOff> ||
                          std::is_same_v<Method, methods::ContrastOff> ||
                          std::is_same_v<Method, methods::SharpenOff>) {
                return {};
            } else if constexpr (std::is_same_v<Method, methods::Tvl1>) {
                auto facts = methods::apply(method, working, context.observed_tvl1_stop);
                if (!facts) {
                    return std::unexpected(std::move(facts.error()));
                }
                store_completed(slot, std::move(*facts));
                context.observed_tvl1_stop.reset();
                return {};
            } else {
                auto facts = methods::apply(method, working);
                if (!facts) {
                    return std::unexpected(std::move(facts.error()));
                }
                store_completed(slot, std::move(*facts));
                return {};
            }
        },
        selected);
}
core::Result<void> restoration(Input& input, const methods::Plan& plan, Frame& frame,
                               methods::Working working) {
    const auto* const method = std::get_if<methods::Wiener>(&plan.photometric.restoration);
    if (method == nullptr) {
        return {};
    }
    frame.failure.failed_stage = methods::Family::restoration;
    auto& context = frame.failure.warning_context;
    context.restoration_entered = true;
    context.restoration_after_transform = methods::transformed_before_restoration(
        frame.failure.completed, frame.rendering.interpretation.orientation, plan.rotation);
    if (auto resolved =
            resolve_psf(input, plan, frame, working.budget.get(), working.scheduler.get());
        !resolved) {
        return resolved;
    }
    if (!frame.psf) {
        return core::failure(core::ErrorCode::invariant, "Resolved restoration PSF is absent");
    }
    auto facts = methods::apply(*method, *frame.psf, working);
    if (!facts) {
        return std::unexpected(std::move(facts.error()));
    }
    store_completed(frame.failure.completed.restoration, *facts);
    frame.psf.reset();
    context.restoration_entered = false;
    context.restoration_after_transform = false;
    context.resolved_psf_centroid.reset();
    frame.failure.failed_stage.reset();
    return {};
}
} // namespace
core::Result<void> apply_stages(Input& input, const methods::Plan& plan, Frame& frame,
                                core::Budget& budget, const exec::Scheduler& scheduler) {
    const methods::Working working{frame.luminance.view(), frame.protection.view().as_const(),
                                   budget, scheduler};
    auto& completed = frame.failure.completed;
    auto& context = frame.failure.warning_context;
    frame.failure.failed_stage = methods::Family::illumination;
    if (auto result =
            apply_family(plan.photometric.illumination, working, completed.illumination, context);
        !result) {
        return result;
    }
    frame.failure.failed_stage.reset();
    frame.failure.failed_stage = methods::Family::denoising;
    if (auto result =
            apply_family(plan.photometric.denoising, working, completed.denoising, context);
        !result) {
        return result;
    }
    frame.failure.failed_stage.reset();
    if (auto result = restoration(input, plan, frame, working); !result) {
        return result;
    }
    frame.failure.failed_stage = methods::Family::contrast;
    if (auto result = apply_family(plan.photometric.contrast, working, completed.contrast, context);
        !result) {
        return result;
    }
    frame.failure.failed_stage.reset();
    frame.failure.failed_stage = methods::Family::sharpening;
    if (const auto* const method = std::get_if<methods::Unsharp>(&plan.photometric.sharpening)) {
        context.positive_sharpening_entered = method->parameters().amount != 0;
    }
    if (auto result =
            apply_family(plan.photometric.sharpening, working, completed.sharpening, context);
        !result) {
        return result;
    }
    context.positive_sharpening_entered = false;
    frame.failure.failed_stage.reset();
    return {};
}
} // namespace docenhance::pipeline
