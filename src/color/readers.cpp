// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "context.hpp"
#include "conversion.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <lcms2.h>
#include <memory>
#include <utility>

namespace docenhance::color {
namespace {
core::Result<void> allocate_rows(InterpretationReaderState& reader, core::Budget& budget) {
    const auto& run = reader.run.get();
    constexpr std::uint32_t rgb_channels{image::rgb_channels};
    const auto channels =
        image::is_color(run.source.get().shape.model) ? rgb_channels : std::uint32_t{1};
    const std::uint32_t colors = conversion_pixels * channels;
    auto input = image::Plane<double>::allocate(budget, colors, 1);
    if (!input) {
        return std::unexpected(input.error());
    }
    reader.input = std::move(*input);
    auto linear = image::Plane<double>::allocate(budget, colors, 1);
    if (!linear) {
        return std::unexpected(linear.error());
    }
    reader.linear = std::move(*linear);
    auto alpha = image::Plane<double>::allocate(budget, conversion_pixels, 1);
    if (!alpha) {
        return std::unexpected(alpha.error());
    }
    reader.alpha = std::move(*alpha);
    return {};
}
core::Result<void> prepare_native(InterpretationReaderState& reader) {
    auto& run = reader.run.get();
    if (!run.native) {
        return {};
    }
    if (run.initial_transform) {
        reader.transform = std::move(run.initial_transform);
        return {};
    }
    const cmsUInt32Number format =
        image::is_color(run.source.get().shape.model) ? TYPE_RGB_DBL : TYPE_GRAY_DBL;
    reader.transform.reset(cmsCreateTransformTHR(
        run.context.get(), run.input_profile.get(), format, run.linear_profile.get(), format,
        INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE));
    if (!reader.transform || !run.context.good()) {
        return std::unexpected(run.context.error("Cannot interpret source color profile"));
    }
    return {};
}
} // namespace
core::Result<void> prepare_readers(InterpretationState& state, core::Budget& budget,
                                   unsigned workers) {
    const std::size_t colors =
        image::is_color(state.source.get().shape.model) ? image::rgb_channels : 1;
    const auto per_reader = ((2 * colors) + 1) * conversion_pixels * sizeof(double);
    const auto affordable = budget.available() / per_reader;
    const auto count = std::min<std::size_t>({workers, state.report.output.height, affordable});
    if (count == 0) {
        return core::failure(core::ErrorCode::resource,
                             "The working-memory budget cannot hold one color reader");
    }
    state.readers.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        if (state.cancellation.requested(core::Checkpoint::allocation)) {
            return core::cancelled();
        }
        auto reader = std::make_unique<InterpretationReaderState>(state);
        if (auto allocated = allocate_rows(*reader, budget); !allocated) {
            return allocated;
        }
        if (auto prepared = prepare_native(*reader); !prepared) {
            return prepared;
        }
        state.readers.push_back(std::move(reader));
    }
    return {};
}
} // namespace docenhance::color
