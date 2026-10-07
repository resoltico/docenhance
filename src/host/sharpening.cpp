// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "sharpening.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
        report.complete = true;
        return std::nullopt;
    }
    auto model = methods::SharpenModel::prepare(
        input.source.get(), input.mask, std::get<methods::Unsharp>(method),
        {budget, cancellation, report,
         input.prepared ? image::RowUse::verification : image::RowUse::output});
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
    const bool observed = prepared();
    auto read = input_.source.get().read(range, rgb, observed ? image::RowUse::verification : use);
    if (!read || !active()) {
        return read;
    }
    auto ignored = report_.get();
    return model_->apply(range, rgb, input_.mask,
                         use == image::RowUse::output ? report_.get() : ignored, cancellation_);
}
core::Result<void> assess_sharpening(SharpenedSource& source, core::Budget& budget,
                                     const core::Cancellation& cancellation,
                                     methods::SharpenReport& report) {
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
    report.status = report.changed_samples == 0 ? methods::SharpenStatus::no_change
                                                : methods::SharpenStatus::applied;
    report.reason = report.changed_samples == 0 ? methods::SharpenReason::no_effect
                                                : methods::SharpenReason::none;
    report.complete = true;
    return {};
}
} // namespace docenhance::host
