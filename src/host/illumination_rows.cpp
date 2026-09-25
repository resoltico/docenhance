// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "illumination_rows.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/raster.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
namespace docenhance::host {
image::OutputDescriptor IlluminationRows::descriptor() const noexcept {
    return source_.get().descriptor();
}
core::Result<void> IlluminationRows::row(std::uint32_t index, std::span<std::uint8_t> bytes,
                                         image::RowUse use) {
    const auto shape = descriptor().shape;
    const auto required = image::raster_row_bytes(shape);
    if (!required || bytes.size() != *required || index >= shape.height) {
        return core::failure(core::ErrorCode::argument, "Illumination output row extent mismatch");
    }
    auto ignored = run_.report.get();
    auto& observations = use == image::RowUse::output ? run_.report.get() : ignored;
    const auto pixel_bytes = image::components(shape.model) * (shape.depth / image::byte_bits);
    for (std::uint32_t first = 0; first < shape.width;) {
        const auto count = std::min(image::linear_block_pixels, shape.width - first);
        const image::RowRange range{.row = index, .first = first};
        auto const rgb = block_.view().row(0).first(std::size_t{count} * image::rgb_channels);
        auto read = source_.get().read(range, rgb, use);
        if (!read) {
            return read;
        }
        auto applied = run_.model.get().apply(range, rgb, run_.protection, observations,
                                              run_.cancellation.get());
        if (!applied) {
            return applied;
        }
        auto quantized = image::quantize_linear(
            shape, rgb,
            bytes.subspan(std::size_t{first} * pixel_bytes, std::size_t{count} * pixel_bytes));
        if (!quantized) {
            return quantized;
        }
        first += count;
    }
    if (use == image::RowUse::output && index + 1 == shape.height) {
        run_.report.get().complete = true;
    }
    return {};
}
} // namespace docenhance::host
