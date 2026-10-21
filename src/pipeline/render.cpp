// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/pipeline/render.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/methods/warnings.hpp"
#include "frame.hpp"

#include <cstdint>
#include <expected>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace docenhance::pipeline {
namespace {
bool ready(const Input& input, const methods::Plan& plan) noexcept {
    if (!input.ready() || !methods::validate(plan) ||
        plan.protection != input.encoded().mask_identity.has_value()) {
        return false;
    }
    const auto* const method = std::get_if<methods::Wiener>(&plan.photometric.restoration);
    const bool kernel =
        method != nullptr && std::holds_alternative<methods::Kernel>(method->parameters().psf);
    return kernel == input.encoded().psf_identity.has_value();
}
} // namespace
RenderResult render(Input input, const methods::Plan& plan, core::Budget& budget,
                    const exec::Scheduler& scheduler) {
    static_assert(std::is_nothrow_move_constructible_v<Failure>);
    static_assert(std::is_nothrow_move_constructible_v<Rendering>);
    Frame frame{
        .source = image::Raster(),
        .interpreter = nullptr,
        .luminance = image::Plane<double>(),
        .protection = image::Plane<std::uint8_t>(),
        .psf = std::nullopt,
        .rendering = Rendering(),
        .failure =
            {
                .error =
                    {
                        .code = core::ErrorCode::resource,
                        .message = "Rendering allocation failed",
                    },
                .failed_stage = std::nullopt,
                .completed = methods::CompletedStages(),
                .warning_context = methods::FailureWarningContext(),
            },
    };
    core::Error unexpected{
        .code = core::ErrorCode::invariant,
        .message = "Unexpected rendering failure",
    };
    try {
        if (!ready(input, plan)) {
            frame.failure.error = {
                .code = core::ErrorCode::argument,
                .message = "Rendering input ownership or resource roles are unready",
            };
            return std::unexpected(std::move(frame.failure));
        }
        if (scheduler.cancellation().requested(core::Checkpoint::processing)) {
            frame.failure.error = core::cancelled().error();
            return std::unexpected(std::move(frame.failure));
        }
        if (auto result = prepare_frame(input, plan, frame, budget, scheduler); !result) {
            frame.failure.error = std::move(result.error());
            return std::unexpected(std::move(frame.failure));
        }
        if (auto result = apply_stages(input, plan, frame, budget, scheduler); !result) {
            frame.failure.error = std::move(result.error());
            return std::unexpected(std::move(frame.failure));
        }
        if (auto result = finish_output(plan, frame, budget, scheduler); !result) {
            frame.failure.error = std::move(result.error());
            return std::unexpected(std::move(frame.failure));
        }
        if (!image::validate(frame.rendering.interpretation) ||
            !methods::validate(frame.failure.completed)) {
            frame.failure.error = {
                .code = core::ErrorCode::invariant,
                .message = "Rendering produced inconsistent completed facts",
            };
            return std::unexpected(std::move(frame.failure));
        }
        frame.rendering.stages = static_cast<methods::StageReports&>(frame.failure.completed);
        return std::move(frame.rendering);
    } catch (const std::bad_alloc&) {
        // The preallocated resource failure and owned partial context survive ordinary unwind.
        return std::unexpected(std::move(frame.failure));
    } catch (...) {
        frame.failure.error = std::move(unexpected);
    }
    return std::unexpected(std::move(frame.failure));
}
} // namespace docenhance::pipeline
