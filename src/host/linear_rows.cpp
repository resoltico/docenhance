// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "linear_rows.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/raster.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
namespace docenhance::host {
core::Result<void> IlluminatedSource::read(image::RowRange range, std::span<double> rgb,
                                           image::RowUse use) {
    const bool output = use == image::RowUse::output;
    if (output && (range.row != next_output_.row || range.first != next_output_.first)) {
        return core::failure(core::ErrorCode::invariant,
                             "Output must traverse each linear sample once in row-major order");
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
            return applied;
        }
    }
    if (output) {
        next_output_.first += static_cast<std::uint32_t>(rgb.size() / image::rgb_channels);
        if (next_output_.first == extent().width) {
            next_output_.first = 0;
            ++next_output_.row;
        }
        if (model_ != nullptr && next_output_.row == extent().height) {
            report_.get().complete = true;
        }
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
                                       prepared_ ? image::RowUse::verification : use);
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
