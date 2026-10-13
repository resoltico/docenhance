// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "sharpening.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "linear_rows.hpp"

#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <variant>
namespace docenhance::host {
core::Result<std::optional<methods::SharpenModel>>
prepare_sharpening(SharpenInput input, const methods::Sharpening& method, core::Budget& budget,
                   const core::Cancellation& cancellation, methods::SharpenReport& report) {
    if (std::holds_alternative<methods::SharpenOff>(method)) {
        report.progress = methods::StageProgress::completed;
        return std::nullopt;
    }
    auto model = methods::SharpenModel::prepare(
        input.source.get(), input.mask, std::get<methods::Unsharp>(method),
        {budget, cancellation, report,
         input.prefix_observed ? image::RowUse::verification : image::RowUse::output});
    if (!model) {
        return std::unexpected(model.error());
    }
    SharpenedSource assessed{input, &*model, report, cancellation};
    if (assessed.active()) {
        auto result = assess_sharpening(assessed, budget, cancellation, report);
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    return std::optional<methods::SharpenModel>{std::move(*model)};
}
core::Result<void> SharpenedSource::read(image::RowRange range, std::span<double> rgb,
                                         image::RowUse use) {
    const bool observed = prefix_observed();
    if (use == image::RowUse::output && active()) {
        report_.get().progress = methods::StageProgress::evaluating;
    }
    auto read = input_.source.get().read(range, rgb, observed ? image::RowUse::verification : use);
    if (!read || !active()) {
        return read;
    }
    auto ignored = report_.get();
    auto applied =
        model_->apply(range, rgb, input_.mask,
                      use == image::RowUse::output ? report_.get() : ignored, cancellation_);
    if (!applied && use == image::RowUse::output) {
        report_.get().status = methods::SharpenStatus::failed;
        report_.get().reason = methods::SharpenReason::processing_failure;
    }
    return applied;
}
core::Result<void> assess_sharpening(SharpenedSource& source, core::Budget& budget,
                                     const core::Cancellation& cancellation,
                                     methods::SharpenReport& report) {
    auto observed =
        observe_rows(source, budget, cancellation, true, &report.preparation_charge_peak);
    if (!observed) {
        if (report.progress == methods::StageProgress::prepared) {
            report.status = methods::SharpenStatus::failed;
            report.reason = methods::SharpenReason::processing_failure;
        }
        return observed;
    }
    report.status = report.changed_samples == 0 ? methods::SharpenStatus::no_change
                                                : methods::SharpenStatus::applied;
    report.reason = report.changed_samples == 0 ? methods::SharpenReason::no_effect
                                                : methods::SharpenReason::none;
    report.progress = methods::StageProgress::completed;
    return {};
}
} // namespace docenhance::host
