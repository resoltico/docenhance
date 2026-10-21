// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "frame.hpp"

#include "docenhance/codec/source.hpp"
#include "docenhance/color/interpretation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/pipeline/render.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <variant>

namespace docenhance::pipeline {
namespace {
constexpr std::size_t band_limit = 64;
core::Result<image::PngShape> output_shape(const image::Raster& source, const methods::Plan& plan) {
    const auto shape = image::rotated_shape(
        image::oriented_shape(source.shape, source.metadata.orientation), plan.rotation);
    unsigned bits = 1;
    auto model = image::SampleModel::gray;
    if (const auto* const continuous = std::get_if<methods::ContinuousOutput>(&plan.output)) {
        bits = source.shape.depth.bits();
        if (continuous->depth == image::OutputDepth::word) {
            bits = image::word_bits;
        } else if (continuous->depth != image::OutputDepth::automatic) {
            bits = image::byte_bits;
        }
        model = continuous->mode == image::ToneMode::gray || !image::is_color(source.shape.model)
                    ? image::SampleModel::gray
                    : image::SampleModel::rgb;
    }
    const auto depth = image::PngDepth::from_bits(bits);
    if (!depth) {
        return core::failure(core::ErrorCode::invariant, "Invalid pipeline output precision");
    }
    return image::PngShape{
        .width = shape.width,
        .height = shape.height,
        .model = model,
        .depth = *depth,
    };
}
core::Result<image::BlockObservation> extract_row(Frame& frame, color::InterpretationReader& reader,
                                                  std::uint32_t y) {
    const bool gray = frame.interpreter->category() == image::InterpretationCategory::gray1;
    const auto colors = gray ? 1 : image::rgb_channels;
    const auto out = frame.luminance.view().row(y);
    const auto scratch = reader.scratch();
    image::BlockObservation observation;
    for (std::uint32_t first = 0; first < out.size();) {
        const auto count =
            std::min(image::linear_block_pixels, static_cast<std::uint32_t>(out.size()) - first);
        const auto block = scratch.first(std::size_t{count} * colors);
        auto read = gray ? reader.read_gray(y, first, block) : reader.read_rgb(y, first, block);
        if (!read) {
            return std::unexpected(std::move(read.error()));
        }
        observation.flattened_pixels += read->flattened_pixels;
        observation.clipped_components += read->clipped_components;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto pixel = block.subspan(std::size_t{i} * colors, colors);
            const auto value = gray ? pixel.front()
                                    : image::luminance({
                                          pixel.front(),
                                          pixel.subspan(1, 1).front(),
                                          pixel.subspan(2, 1).front(),
                                      });
            if (!std::isfinite(value) || value < 0 || value > 1) {
                return core::failure(core::ErrorCode::invariant,
                                     "Interpretation produced invalid unit luminance");
            }
            out.subspan(first + i, 1).front() = value;
        }
        first += count;
    }
    return observation;
}
core::Result<void> extract(Frame& frame, const exec::Scheduler& schedule) {
    const auto concurrency = exec::Concurrency::resolve(frame.interpreter->workers(), 1, 0, 0);
    if (!concurrency) {
        return std::unexpected(concurrency.error());
    }
    const exec::Scheduler scheduler{*concurrency, schedule.cancellation()};
    const auto height = frame.luminance.height();
    const auto bands = std::min(band_limit, std::size_t{height});
    std::array<image::BlockObservation, band_limit> partials{};
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto first = static_cast<std::uint32_t>(std::uint64_t{band} * height / bands);
        const auto last = static_cast<std::uint32_t>(std::uint64_t{band + 1} * height / bands);
        auto reader = frame.interpreter->reader();
        if (!reader) {
            return std::unexpected(reader.error());
        }
        image::BlockObservation partial;
        for (auto y = first; y < last; ++y) {
            auto row = extract_row(frame, *reader, y);
            if (!row) {
                return std::unexpected(std::move(row.error()));
            }
            partial.flattened_pixels += row->flattened_pixels;
            partial.clipped_components += row->clipped_components;
        }
        partials.at(band) = partial;
        return {};
    };
    if (auto extracted = scheduler.for_each(bands, exec::WorkRef{work}); !extracted) {
        return extracted;
    }
    for (const auto partial : std::span{partials}.first(bands)) {
        frame.rendering.interpretation.flattened_pixels += partial.flattened_pixels;
        frame.rendering.interpretation.clipped_components += partial.clipped_components;
    }
    frame.failure.warning_context.interpretation = frame.rendering.interpretation;
    return {};
}
} // namespace
core::Result<void> prepare_frame(Input& input, const methods::Plan& plan, Frame& frame,
                                 core::Budget& budget, const exec::Scheduler& scheduler) {
    auto decoded = codec::decode_source(input.encoded().source.bytes(), budget, plan.profile,
                                        scheduler.cancellation());
    if (!decoded) {
        return std::unexpected(std::move(decoded.error()));
    }
    input.encoded().source = core::Buffer{};
    frame.rendering.source = decoded->description;
    frame.source = std::move(decoded->raster);
    auto output = output_shape(frame.source, plan);
    if (!output) {
        return std::unexpected(std::move(output.error()));
    }
    auto interpreter = color::Interpretation::create(
        frame.source, {.alpha = plan.alpha, .profile = plan.profile, .rotation = plan.rotation},
        *output, budget,
        {
            .workers =
                static_cast<unsigned>(std::min(band_limit, std::size_t{scheduler.workers()})),
            .cancellation = scheduler.cancellation(),
        });
    if (!interpreter) {
        return std::unexpected(std::move(interpreter.error()));
    }
    frame.interpreter = std::move(*interpreter);
    frame.rendering.interpretation = frame.interpreter->descriptor();
    if (auto mask = prepare_mask(input, plan, frame, budget, scheduler); !mask) {
        return mask;
    }
    const auto shape = frame.rendering.interpretation.output;
    auto plane = image::Plane<double>::allocate(budget, shape.width, shape.height);
    if (!plane) {
        return std::unexpected(std::move(plane.error()));
    }
    frame.luminance = std::move(*plane);
    if (auto extracted = extract(frame, scheduler); !extracted) {
        return extracted;
    }
    if (shape.model != image::SampleModel::rgb) {
        frame.interpreter.reset();
        frame.source = {};
    }
    return {};
}
} // namespace docenhance::pipeline
