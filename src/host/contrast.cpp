// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "contrast.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <variant>
namespace docenhance::host {
core::Result<std::optional<methods::ContrastModel>>
prepare_contrast(ContrastInput input, const methods::Contrast& method, core::Budget& budget,
                 const core::Cancellation& cancellation, methods::ContrastReport& report) {
    if (std::holds_alternative<methods::ContrastOff>(method)) {
        report.complete = true;
        return std::nullopt;
    }
    auto model = methods::ContrastModel::prepare(
        input.source.get(), input.mask, method,
        {budget, cancellation, report,
         input.prepared ? image::RowUse::verification : image::RowUse::output});
    if (!model) {
        return std::unexpected(model.error());
    }
    return std::optional<methods::ContrastModel>{*model};
}
core::Result<void> ContrastedSource::read(image::RowRange range, std::span<double> rgb,
                                          image::RowUse use) {
    const bool observed = input_.prepared || (model_ != nullptr && model_->measured_source());
    auto read = input_.source.get().read(range, rgb, observed ? image::RowUse::verification : use);
    if (!read || !active()) {
        return read;
    }
    auto ignored = report_.get();
    return model_->apply(range, rgb, input_.mask,
                         use == image::RowUse::output ? report_.get() : ignored, cancellation_);
}
core::Result<void> assess_contrast(ContrastedSource& source, core::Budget& budget,
                                   const core::Cancellation& cancellation,
                                   methods::ContrastReport& report) {
    if (cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto block =
        image::Plane<double>::allocate(budget, image::linear_block_pixels * image::rgb_channels, 1);
    if (!block) {
        return std::unexpected(block.error());
    }
    report.preparation_charge_peak =
        std::max(report.preparation_charge_peak, static_cast<std::uint64_t>(budget.used()));
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        for (std::uint32_t first = 0; first < source.extent().width;) {
            if (cancellation.requested(core::Checkpoint::processing)) {
                return core::cancelled();
            }
            const auto n = std::min(image::linear_block_pixels, source.extent().width - first);
            auto read =
                source.read({.row = y, .first = first},
                            block->view().row(0).first(std::size_t{n} * image::rgb_channels),
                            image::RowUse::output);
            if (!read) {
                return std::unexpected(read.error());
            }
            first += n;
        }
    }
    report.status = report.changed_samples == 0 ? methods::ContrastStatus::no_change
                                                : methods::ContrastStatus::applied;
    report.reason = report.changed_samples == 0 ? methods::ContrastReason::no_effect
                                                : methods::ContrastReason::none;
    report.complete = true;
    return {};
}
} // namespace docenhance::host
