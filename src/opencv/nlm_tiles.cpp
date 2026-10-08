// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/opencv/nlm.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
namespace docenhance::opencv {
namespace {
// Advancing by the remaining region reaches UINT32_MAX exactly without wrapping past it.
constexpr std::uint32_t tile_length(std::uint32_t first, std::uint32_t extent) noexcept {
    return std::min(methods::nlm_tile_width, extent - first);
}
constexpr std::uint32_t last_tile_samples = 3;
static_assert(tile_length(std::numeric_limits<std::uint32_t>::max() - last_tile_samples,
                          std::numeric_limits<std::uint32_t>::max()) == last_tile_samples);
static_assert(tile_length(0, std::numeric_limits<std::uint32_t>::max()) == methods::nlm_tile_width);
struct TileRegion {
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t radius;
};
core::Result<void> fill_tile(image::PlaneView<const std::uint16_t> input,
                             image::PlaneView<std::uint16_t> tile, TileRegion region,
                             const core::Cancellation& cancellation) {
    for (std::uint32_t y = 0; y < tile.height(); ++y) {
        if (cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const auto iy = image::reflect101_folded(
            static_cast<std::int64_t>(region.y) + y - region.radius, input.height());
        const auto row = input.row(static_cast<std::uint32_t>(iy));
        for (std::uint32_t x = 0; x < tile.width(); ++x) {
            const auto ix = image::reflect101_folded(
                static_cast<std::int64_t>(region.x) + x - region.radius, input.width());
            tile.row(y).subspan(x, 1).front() = row.subspan(ix, 1).front();
        }
    }
    return {};
}
void copy_center(image::PlaneView<const std::uint16_t> tile, image::PlaneView<std::uint16_t> output,
                 TileRegion region) {
    for (std::uint32_t y = 0; y < region.height; ++y) {
        std::ranges::copy(tile.row(y + region.radius).subspan(region.radius, region.width),
                          output.row(region.y + y).subspan(region.x, region.width).begin());
    }
}
core::Result<image::PlaneView<std::uint16_t>> tile_view(image::Plane<std::uint16_t>& plane,
                                                        TileRegion region) {
    return image::PlaneView<std::uint16_t>::create(
        plane.view().storage(), {
                                    .width = region.width + (2 * region.radius),
                                    .height = region.height + (2 * region.radius),
                                    .stride = plane.view().row_pitch() * sizeof(std::uint16_t),
                                });
}

struct TileBuffers {
    std::reference_wrapper<image::Plane<std::uint16_t>> input;
    std::reference_wrapper<image::Plane<std::uint16_t>> output;
};
core::Result<void> run_tile(image::PlaneView<const std::uint16_t> input,
                            image::PlaneView<std::uint16_t> output, TileBuffers buffers,
                            TileRegion region, NlmExecution execution) {
    auto tile_in = tile_view(buffers.input.get(), region);
    auto tile_out = tile_view(buffers.output.get(), region);
    if (!tile_in) {
        return std::unexpected(tile_in.error());
    }
    if (!tile_out) {
        return std::unexpected(tile_out.error());
    }
    auto filled = fill_tile(input, *tile_in, region, execution.cancellation.get());
    if (!filled) {
        return filled;
    }
    auto result =
        native_tile(tile_in->as_const(), *tile_out, execution.method.get(), execution.budget.get());
    if (!result) {
        return std::unexpected(result.error());
    }
    execution.report.get().native_reserved_peak =
        std::max(execution.report.get().native_reserved_peak,
                 static_cast<std::uint64_t>(result->reserved_bytes));
    execution.report.get().preparation_charge_peak =
        std::max(execution.report.get().preparation_charge_peak,
                 static_cast<std::uint64_t>(result->charged_bytes));
    ++execution.report.get().native_calls;
    if (execution.cancellation.get().requested(core::Checkpoint::processing)) {
        return core::cancelled();
    }
    copy_center(tile_out->as_const(), output, region);
    return {};
}
} // namespace
core::Result<void> denoise(image::PlaneView<const std::uint16_t> input,
                           image::PlaneView<std::uint16_t> output, NlmExecution execution) {
    const auto& method = execution.method.get();
    auto& budget = execution.budget.get();
    if (input.empty() || input.width() != output.width() || input.height() != output.height() ||
        image::overlaps(input, output)) {
        return core::failure(core::ErrorCode::argument, "NLM plane extent or overlap mismatch");
    }
    const auto p = method.parameters();
    if (p.search > std::min(input.width(), input.height())) {
        return core::failure(core::ErrorCode::method_inapplicable,
                             "NLM search must fit the oriented image");
    }
    const auto radius = (p.search + p.patch - 2) / 2;
    const auto width = std::min(methods::nlm_tile_width, input.width()) + (2 * radius);
    const auto height = std::min(methods::nlm_tile_width, input.height()) + (2 * radius);
    auto in = image::Plane<std::uint16_t>::allocate(budget, width, height);
    if (!in) {
        return std::unexpected(in.error());
    }
    auto out = image::Plane<std::uint16_t>::allocate(budget, width, height);
    if (!out) {
        return std::unexpected(out.error());
    }
    for (std::uint32_t y = 0; y < input.height(); y += tile_length(y, input.height())) {
        for (std::uint32_t x = 0; x < input.width(); x += tile_length(x, input.width())) {
            const TileRegion region{
                .x = x,
                .y = y,
                .width = tile_length(x, input.width()),
                .height = tile_length(y, input.height()),
                .radius = radius,
            };
            auto tile = run_tile(input, output, {.input = *in, .output = *out}, region, execution);
            if (!tile) {
                return tile;
            }
        }
    }
    return {};
}
} // namespace docenhance::opencv
