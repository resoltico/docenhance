// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source.hpp"
#include "morphology_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
namespace docenhance::methods {
namespace {
bool protected_at(image::PlaneView<const std::uint8_t> mask, std::uint32_t x,
                  std::uint32_t y) noexcept {
    return !mask.empty() && mask.row(y).subspan(x, 1).front() != 0;
}
core::Result<double> luminance_at(std::span<const double> rgb, std::uint32_t x) {
    const auto p = rgb.subspan(std::size_t{x} * image::rgb_channels, image::rgb_channels);
    return image::luminance({p.front(), p.subspan(1, 1).front(), p.subspan(2, 1).front()});
}
struct SamplePass {
    std::span<double> values;
    std::span<double> rgb;
    image::PlaneView<const double> background;
    std::uint32_t count{};
};
struct SampleBlock {
    std::uint32_t row{};
    std::uint32_t first{};
    std::uint32_t stride{};
};
core::Result<void> gather_block(MorphologyContext context, SamplePass& samples, SampleBlock block) {
    if (context.cancellation.get().requested(core::Checkpoint::measurement)) {
        return core::cancelled();
    }
    const auto count = std::min(image::linear_block_pixels,
                                context.input.source.get().extent().width - block.first);
    const auto rgb = samples.background.empty()
                         ? samples.rgb.first(std::size_t{count} * image::rgb_channels)
                         : std::span<double>{};
    if (samples.background.empty()) {
        auto read = context.input.source.get().read({.row = block.row, .first = block.first}, rgb,
                                                    image::RowUse::measurement);
        if (!read) {
            return read;
        }
    }
    for (std::uint32_t i = 0; i < count && samples.count < samples.values.size(); ++i) {
        const auto x = block.first + i;
        if (x % block.stride != 0 || protected_at(context.input.protection, x, block.row)) {
            continue;
        }
        const auto value =
            samples.background.empty()
                ? luminance_at(rgb, i)
                : core::Result<double>{samples.background.row(block.row).subspan(x, 1).front()};
        if (!value) {
            return std::unexpected(value.error());
        }
        samples.values.subspan(samples.count++, 1).front() = *value;
    }
    return {};
}
core::Result<void> gather(MorphologyContext context, SamplePass& samples, std::uint32_t stride) {
    const auto extent = context.input.source.get().extent();
    for (std::uint64_t y = 0; y < extent.height && samples.count < samples.values.size();
         y += stride) {
        for (std::uint32_t first = 0; first < extent.width && samples.count < samples.values.size();
             first += std::min(image::linear_block_pixels, extent.width - first)) {
            auto read = gather_block(
                context, samples,
                {.row = static_cast<std::uint32_t>(y), .first = first, .stride = stride});
            if (!read) {
                return read;
            }
        }
    }
    return {};
}
core::Result<std::uint64_t> protected_samples(MorphologyContext context) {
    const auto mask = context.input.protection;
    std::uint64_t count = 0;
    for (std::uint32_t y = 0; y < mask.height(); ++y) {
        for (std::uint32_t x = 0; x < mask.width(); ++x) {
            constexpr std::uint32_t interval = 1024;
            if (x % interval == 0 &&
                context.cancellation.get().requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            count += static_cast<std::uint64_t>(protected_at(mask, x, y));
        }
    }
    return count;
}
core::Result<void> fill_block(MorphologyContext context, image::PlaneView<double> output,
                              std::span<double> rgb, image::RowRange range, double fill) {
    const auto read = context.input.source.get().read(range, rgb, image::RowUse::measurement);
    if (!read) {
        return read;
    }
    const auto count = static_cast<std::uint32_t>(rgb.size() / image::rgb_channels);
    for (std::uint32_t x = 0; x < count; ++x) {
        const auto value = protected_at(context.input.protection, range.first + x, range.row)
                               ? core::Result<double>{fill}
                               : luminance_at(rgb, x);
        if (!value) {
            return std::unexpected(value.error());
        }
        output.row(range.row).subspan(range.first + x, 1).front() = *value;
    }
    return {};
}
} // namespace
core::Result<void> morphology_counts(MorphologyContext context) {
    const auto extent = context.input.source.get().extent();
    const auto mask = context.input.protection;
    if (extent.width == 0 || extent.height == 0 ||
        (!mask.empty() && (mask.width() != extent.width || mask.height() != extent.height))) {
        return core::failure(core::ErrorCode::argument, "I02 source and protection extents differ");
    }
    const auto total = std::uint64_t{extent.width} * extent.height;
    if (total > image::source_pixels_max) {
        return core::failure(core::ErrorCode::resource, "I02 pixel limit exceeded");
    }
    const auto protected_count = protected_samples(context);
    if (!protected_count) {
        return std::unexpected(protected_count.error());
    }
    context.report.get().protected_samples = *protected_count;
    context.report.get().eligible_samples = total - *protected_count;
    return {};
}
core::Result<MorphologySampling> morphology_samples(MorphologyContext context,
                                                    std::span<double> values, std::span<double> rgb,
                                                    image::PlaneView<const double> background) {
    const auto extent = context.input.source.get().extent();
    const auto maximum = std::max(extent.width, extent.height);
    const auto stride =
        std::max(1U, (maximum / morphology_lattice_extent) +
                         static_cast<std::uint32_t>(maximum % morphology_lattice_extent != 0));
    SamplePass samples{.values = values, .rgb = rgb, .background = background};
    auto result = gather(context, samples, stride);
    if (!result) {
        return std::unexpected(result.error());
    }
    const bool fallback = samples.count == 0;
    if (fallback) {
        samples.values = values.first(1);
        result = gather(context, samples, 1);
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    if (samples.count == 0) {
        return core::failure(core::ErrorCode::method_inapplicable,
                             "I02 has no eligible measurements");
    }
    return MorphologySampling{.stride = stride, .count = samples.count, .fallback = fallback};
}
core::Result<void> morphology_fill(MorphologyContext context, image::PlaneView<double> output,
                                   std::span<double> rgb, double fill) {
    const auto extent = context.input.source.get().extent();
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t first = 0; first < extent.width;) {
            if (context.cancellation.get().requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            const auto count = std::min(image::linear_block_pixels, extent.width - first);
            auto const block = rgb.first(std::size_t{count} * image::rgb_channels);
            const auto read = fill_block(context, output, block, {.row = y, .first = first}, fill);
            if (!read) {
                return read;
            }
            first += count;
        }
    }
    return {};
}
} // namespace docenhance::methods
