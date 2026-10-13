// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "contrast.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/progress.hpp"
#include "linear_rows.hpp"

#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <variant>
namespace docenhance::host {
core::Result<std::optional<methods::ContrastModel>>
prepare_contrast(ContrastInput input, const methods::Contrast& method, core::Budget& budget,
                 const core::Cancellation& cancellation, methods::ContrastReport& report) {
    if (std::holds_alternative<methods::ContrastOff>(method)) {
        report.progress = methods::StageProgress::completed;
        return std::nullopt;
    }
    auto model = methods::ContrastModel::prepare(
        input.source.get(), input.mask, method,
        {budget, cancellation, report,
         input.prefix_observed ? image::RowUse::verification : image::RowUse::output});
    if (!model) {
        return std::unexpected(model.error());
    }
    ContrastedSource assessed{input, &*model, report, cancellation};
    if (assessed.active()) {
        auto result = assess_contrast(assessed, budget, cancellation, report);
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    return std::optional<methods::ContrastModel>{std::move(*model)};
}
core::Result<void> ContrastedSource::read(image::RowRange range, std::span<double> rgb,
                                          image::RowUse use) {
    const bool observed =
        input_.prefix_observed || (model_ != nullptr && model_->measured_source());
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
        report_.get().status = methods::ContrastStatus::failed;
        report_.get().reason = methods::ContrastReason::processing_failure;
    }
    return applied;
}
core::Result<void> assess_contrast(ContrastedSource& source, core::Budget& budget,
                                   const core::Cancellation& cancellation,
                                   methods::ContrastReport& report) {
    auto observed =
        observe_rows(source, budget, cancellation, true, &report.preparation_charge_peak);
    if (!observed) {
        if (report.progress == methods::StageProgress::prepared) {
            report.status = methods::ContrastStatus::failed;
            report.reason = methods::ContrastReason::processing_failure;
        }
        return observed;
    }
    report.status = report.changed_samples == 0 ? methods::ContrastStatus::no_change
                                                : methods::ContrastStatus::applied;
    report.reason = report.changed_samples == 0 ? methods::ContrastReason::no_effect
                                                : methods::ContrastReason::none;
    report.progress = methods::StageProgress::completed;
    return {};
}
} // namespace docenhance::host
