// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/output.hpp"

#include "docenhance/codec/png_output.hpp"
#include "docenhance/color/interpretation.hpp"
#include "docenhance/color/profile.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/pipeline/render.hpp"
#include "failure_context.hpp"
#include "frame.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>

namespace docenhance::pipeline {
namespace {
constexpr std::size_t band_limit = 64;
constexpr double quantization_half = 0.5;
core::Result<void> terminal_row(std::span<const double> input, std::span<std::uint16_t> output,
                                const core::Cancellation& cancellation) {
    for (std::size_t x = 0; x < input.size(); ++x) {
        constexpr std::uint32_t interval = 1024;
        if (x % interval == 0 && cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const auto value = input.subspan(x, 1).front();
        if (!std::isfinite(value) || value < 0 || value > 1) {
            return core::failure(core::ErrorCode::invariant, "Invalid final unit luminance");
        }
        output.subspan(x, 1).front() = static_cast<std::uint16_t>(
            std::floor((image::word_max * image::srgb_encode(value)) + quantization_half));
    }
    return {};
}

core::Result<image::Plane<std::uint16_t>> terminal_codes(Frame& frame, core::Budget& budget,
                                                         const exec::Scheduler& scheduler) {
    const auto width = frame.luminance.width();
    const auto height = frame.luminance.height();
    auto codes = image::Plane<std::uint16_t>::allocate(budget, width, height);
    if (!codes) {
        return std::unexpected(std::move(codes.error()));
    }
    const auto bands = std::min(band_limit, std::size_t{height});
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto first = static_cast<std::uint32_t>(std::uint64_t{band} * height / bands);
        const auto last = static_cast<std::uint32_t>(std::uint64_t{band + 1} * height / bands);
        for (auto y = first; y < last; ++y) {
            if (auto converted = terminal_row(frame.luminance.view().row(y), codes->view().row(y),
                                              scheduler.cancellation());
                !converted) {
                return converted;
            }
        }
        return {};
    };
    if (auto result = scheduler.for_each(bands, exec::WorkRef{work}); !result) {
        return std::unexpected(std::move(result.error()));
    }
    return std::move(*codes);
}
core::Result<image::Plane<std::uint8_t>> binary_output(const methods::BinaryOutput& selected,
                                                       Frame& frame, core::Budget& budget,
                                                       const exec::Scheduler& scheduler) {
    auto codes = terminal_codes(frame, budget, scheduler);
    if (!codes) {
        return std::unexpected(std::move(codes.error()));
    }
    frame.luminance = {};
    auto pixels = image::Plane<std::uint8_t>::allocate(budget, codes->width(), codes->height());
    if (!pixels) {
        return std::unexpected(std::move(pixels.error()));
    }
    const methods::BinaryInput input{codes->view().as_const(), pixels->view(), budget, scheduler};
    frame.failure.failed_stage = methods::Family::binarization;
    auto classified = std::visit(
        [&](const auto& method) -> core::Result<void> {
            auto facts = methods::binarize(method, input);
            if (!facts) {
                return std::unexpected(std::move(facts.error()));
            }
            store_completed(frame.failure.completed.binarization, std::move(*facts));
            return {};
        },
        selected.method);
    if (!classified) {
        return std::unexpected(std::move(classified.error()));
    }
    frame.failure.failed_stage.reset();
    return std::move(*pixels);
}
core::Result<void> continuous_samples(std::span<const double> source, std::span<const double> block,
                                      std::span<std::uint8_t> output, image::PngShape shape) {
    const auto depth = image::SampleDepth::from_bits(shape.depth.bits());
    if (!depth) {
        return core::failure(core::ErrorCode::invariant, "Invalid continuous output precision");
    }
    const auto maximum = *depth == image::SampleDepth::word() ? image::word_max : image::byte_max;
    const auto colors = image::components(shape.model);
    for (std::size_t i = 0; i < source.size(); ++i) {
        const auto value = source.subspan(i, 1).front();
        if (!std::isfinite(value) || value < 0 || value > 1) {
            return core::failure(core::ErrorCode::invariant, "Invalid final unit luminance");
        }
        image::Rgb values{};
        if (colors == 1) {
            values.front() = value;
        } else {
            const auto pixel = block.subspan(i * colors, colors);
            values = {
                pixel.front(),
                pixel.subspan(1, 1).front(),
                pixel.subspan(2, 1).front(),
            };
            if (image::luminance(values) != value) {
                values = image::transport_luminance(values, value);
            }
        }
        for (unsigned c = 0; c < colors; ++c) {
            const auto sample = static_cast<std::uint16_t>(
                std::floor((maximum * image::srgb_encode(values.at(c))) + quantization_half));
            image::write_sample(output.subspan(((i * colors) + c) * depth->bytes(), depth->bytes()),
                                *depth, sample);
        }
    }
    return {};
}
core::Result<void> continuous_row(Frame& frame, image::PlaneView<std::uint8_t> output,
                                  std::uint32_t y, color::InterpretationReader* const reader,
                                  const core::Cancellation& cancellation) {
    const auto shape = frame.rendering.interpretation.output;
    const auto source = frame.luminance.view().row(y);
    const auto out = output.row(y);
    const auto colors = image::components(shape.model);
    const auto sample_bytes = shape.depth.bits() / image::byte_bits;
    const auto scratch = reader == nullptr ? std::span<double>{} : reader->scratch();
    if (colors != 1 && reader == nullptr) {
        return core::failure(core::ErrorCode::invariant, "Color output requires a leased reader");
    }
    for (std::uint32_t first = 0; first < shape.width;) {
        if (cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const auto count = std::min(image::linear_block_pixels, shape.width - first);
        const auto block =
            colors == 1 ? std::span<double>{} : scratch.first(std::size_t{count} * colors);
        if (colors != 1) {
            if (auto observed = reader->read_rgb(y, first, block); !observed) {
                return std::unexpected(std::move(observed.error()));
            }
        }
        if (auto converted =
                continuous_samples(source.subspan(first, count), block,
                                   out.subspan(std::size_t{first} * colors * sample_bytes,
                                               std::size_t{count} * colors * sample_bytes),
                                   shape);
            !converted) {
            return converted;
        }
        first += count;
    }
    return {};
}
core::Result<image::Plane<std::uint8_t>> continuous_output(Frame& frame, core::Budget& budget,
                                                           const exec::Scheduler& schedule) {
    const auto shape = frame.rendering.interpretation.output;
    const auto row_bytes =
        shape.width * image::components(shape.model) * (shape.depth.bits() / image::byte_bits);
    auto output = image::Plane<std::uint8_t>::allocate(budget, row_bytes, shape.height);
    if (!output) {
        return std::unexpected(std::move(output.error()));
    }
    const bool color_output = shape.model == image::SampleModel::rgb;
    const auto concurrency = exec::Concurrency::resolve(
        color_output ? std::min(schedule.workers(), frame.interpreter->workers())
                     : schedule.workers(),
        1, 0, 0);
    if (!concurrency) {
        return std::unexpected(concurrency.error());
    }
    const exec::Scheduler scheduler{*concurrency, schedule.cancellation()};
    const auto bands = std::min(band_limit, std::size_t{shape.height});
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto first = static_cast<std::uint32_t>(std::uint64_t{band} * shape.height / bands);
        const auto last =
            static_cast<std::uint32_t>(std::uint64_t{band + 1} * shape.height / bands);
        std::optional<color::InterpretationReader> reader;
        if (color_output) {
            auto leased = frame.interpreter->reader();
            if (!leased) {
                return std::unexpected(leased.error());
            }
            reader.emplace(std::move(*leased));
        }
        for (auto y = first; y < last; ++y) {
            if (auto result = continuous_row(frame, output->view(), y,
                                             reader ? std::addressof(*reader) : nullptr,
                                             scheduler.cancellation());
                !result) {
                return result;
            }
        }
        return {};
    };
    if (auto result = scheduler.for_each(bands, exec::WorkRef{work}); !result) {
        return std::unexpected(std::move(result.error()));
    }
    return std::move(*output);
}
} // namespace
core::Result<void> finish_output(const methods::Plan& plan, Frame& frame, core::Budget& budget,
                                 const exec::Scheduler& scheduler) {
    frame.protection = {};
    const auto* const binary = std::get_if<methods::BinaryOutput>(&plan.output);
    auto pixels = binary != nullptr ? binary_output(*binary, frame, budget, scheduler)
                                    : continuous_output(frame, budget, scheduler);
    if (!pixels) {
        return std::unexpected(std::move(pixels.error()));
    }
    frame.interpreter.reset();
    frame.source = {};
    frame.luminance = {};
    const auto& description = frame.rendering.interpretation;
    core::Buffer profile;
    if (binary == nullptr) {
        auto created = color::make_output_profile(
            budget, description.output.model == image::SampleModel::gray, scheduler.cancellation());
        if (!created) {
            return std::unexpected(std::move(created.error()));
        }
        profile = std::move(*created);
    }
    const codec::PngImage intended{
        .shape = description.output,
        .pixels = pixels->view().as_const(),
        .profile = profile.bytes(),
        .resolution = description.resolution,
    };
    auto encoded = codec::encode_png(intended, budget, scheduler.cancellation());
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    if (auto compared =
            codec::compare_png(encoded->bytes(), intended, budget, scheduler.cancellation());
        !compared) {
        return compared;
    }
    frame.rendering.output = {
        .shape = description.output,
        .profile_embedded = !profile.empty(),
        .resolution = description.resolution,
        .decoded_and_compared = true,
    };
    frame.rendering.result = std::move(*encoded);
    return {};
}
} // namespace docenhance::pipeline
