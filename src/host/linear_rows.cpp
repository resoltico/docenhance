// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "linear_rows.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/progress.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <variant>
namespace docenhance::host {
core::Result<void> observe_rows(image::LinearSource& source, core::Budget& budget,
                                const core::Cancellation& cancellation, bool allocation_checkpoint,
                                std::uint64_t* live_peak) {
    if (allocation_checkpoint && cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto block =
        image::Plane<double>::allocate(budget, image::linear_block_pixels * image::rgb_channels, 1);
    if (!block) {
        return std::unexpected(block.error());
    }
    if (live_peak != nullptr) {
        *live_peak = std::max(*live_peak, static_cast<std::uint64_t>(budget.used()));
    }
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
                return read;
            }
            first += n;
        }
    }
    return {};
}
void IlluminatedSource::advance_output(std::uint32_t count) noexcept {
    next_output_.first += count;
    if (next_output_.first == extent().width) {
        next_output_.first = 0;
        ++next_output_.row;
    }
    if (model_ != nullptr && next_output_.row == extent().height) {
        auto& r = report_.get();
        r.progress = methods::StageProgress::completed;
        r.status = r.changed_samples == 0 ? methods::IlluminationStatus::no_change
                                          : methods::IlluminationStatus::applied;
        r.reason = r.changed_samples == 0 ? methods::IlluminationReason::no_effect
                                          : methods::IlluminationReason::none;
    }
}
core::Result<void> IlluminatedSource::read(image::RowRange range, std::span<double> rgb,
                                           image::RowUse use) {
    const bool output = use == image::RowUse::output;
    if (output && (range.row != next_output_.row || range.first != next_output_.first)) {
        return core::failure(core::ErrorCode::invariant,
                             "Output must traverse each linear sample once in row-major order");
    }
    if (output && model_ != nullptr) {
        report_.get().progress = methods::StageProgress::evaluating;
    }
    auto read = source_.get().read(range, rgb, use);
    if (!read) {
        return read;
    }
    if (model_ != nullptr) {
        auto ignored = report_.get();
        auto& observations = output ? report_.get() : ignored;
        auto applied = std::visit(
            [&](const auto& model) {
                return model.apply(range, rgb, protection_, observations, cancellation_);
            },
            *model_);
        if (!applied) {
            if (output) {
                report_.get().status = methods::IlluminationStatus::failed;
                report_.get().reason = methods::IlluminationReason::processing_failure;
            }
            return applied;
        }
    }
    if (output) {
        advance_output(static_cast<std::uint32_t>(rgb.size() / image::rgb_channels));
    }
    return {};
}

core::Result<void> ContinuousRows::row(std::uint32_t index, std::span<std::uint8_t> bytes,
                                       image::RowUse use) {
    const auto shape = descriptor_.shape;
    const auto required = image::raster_row_bytes(shape);
    if (!required || bytes.size() != *required || index >= shape.height) {
        return core::failure(core::ErrorCode::argument, "Continuous row extent mismatch");
    }
    const auto pixel_bytes = image::components(shape.model) * shape.depth.bytes();
    for (std::uint32_t first = 0; first < shape.width;) {
        const auto count = std::min(image::linear_block_pixels, shape.width - first);
        auto const rgb = block_.view().row(0).first(std::size_t{count} * image::rgb_channels);
        auto read = source_.get().read({.row = index, .first = first}, rgb,
                                       prefix_observed_ ? image::RowUse::verification : use);
        if (!read) {
            return read;
        }
        auto quantized = image::quantize_linear(
            shape, rgb,
            bytes.subspan(std::size_t{first} * pixel_bytes, std::size_t{count} * pixel_bytes));
        if (!quantized) {
            return quantized;
        }
        first += count;
    }
    return {};
}
} // namespace docenhance::host
