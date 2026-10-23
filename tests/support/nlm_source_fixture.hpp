// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/color/interpretation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/nlm_working.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <utility>
namespace docenhance::tests {
inline constexpr image::Extent nlm_source_extent{.width = 32, .height = 32};
inline constexpr std::size_t nlm_source_budget = std::size_t{16} * 1024 * 1024;
// Actual public raster/interpreter owners replace the old virtual RGB producer and lazy model.
// Destruction order keeps the raster alive until the borrowing interpreter has been destroyed.
struct NlmAssociatedFault {
    std::uint32_t x;
    unsigned channel;
};
struct NlmSamplePosition {
    std::uint32_t y;
    std::uint32_t x;
    unsigned channel;
};
inline unsigned associated_sample(std::optional<NlmAssociatedFault> fault, NlmSamplePosition at) {
    if (!fault || at.channel == 3) {
        return 32768U;
    }
    if (at.y == 0 && at.x == fault->x && at.channel == fault->channel) {
        return 60000U;
    }
    return 8192U;
}
struct NlmSourceFixture {
    explicit NlmSourceFixture(core::Budget& owner,
                              image::SampleModel model = image::SampleModel::rgb,
                              const core::Cancellation& cancellation = {},
                              std::optional<NlmAssociatedFault> fault = std::nullopt)
        : budget(owner), luminance(image::Plane<double>::allocate(budget, 32, 32).value()),
          protection(image::Plane<std::uint8_t>::allocate(budget, 32, 32).value()),
          block(image::Plane<double>::allocate(budget, 32 * (image::is_color(model) ? 3 : 1), 1)
                    .value()),
          scheduler(exec::Concurrency::resolve(1, 1, 0, 0).value(), cancellation) {
        source.shape = {
            .width = 32,
            .height = 32,
            .model = model,
            .depth = image::SampleDepth::word(),
        };
        source.pixels = image::Plane<std::uint8_t>::allocate(
                            budget, image::raster_row_bytes(source.shape).value(), 32)
                            .value();
        const auto channels = image::components(model);
        for (std::uint32_t y = 0; y < 32; ++y) {
            const auto row = source.pixels.view().row(y);
            for (std::uint32_t x = 0; x < 32; ++x) {
                for (unsigned c = 0; c < channels; ++c) {
                    const auto sample = associated_sample(fault, {.y = y, .x = x, .channel = c});
                    const auto bytes = row.subspan(((std::size_t{x} * channels) + c) * 2, 2);
                    bytes.front() = static_cast<std::uint8_t>(sample >> 8U);
                    bytes.back() = static_cast<std::uint8_t>(sample & 255U);
                }
            }
        }
        if (fault) {
            source.metadata.declarations = image::TiffDeclarations{.associated_alpha = true};
        }
        std::ranges::fill(luminance.view().storage(), -2);
        std::ranges::fill(protection.view().storage(), std::uint8_t{255});
        for (std::uint32_t y = 0; y < 32; ++y) {
            std::ranges::fill(protection.view().row(y), std::uint8_t{0});
        }
        protection.view().row(0).front() = 255;
        const image::InterpretationPolicy policy{};
        interpreter = color::Interpretation::create(source, policy,
                                                    {
                                                        .width = 32,
                                                        .height = 32,
                                                        .model = image::SampleModel::gray,
                                                        .depth = image::PngDepth(),
                                                    },
                                                    budget, {.cancellation = cancellation})
                          .value();
    }
    ~NlmSourceFixture() = default;
    NlmSourceFixture(const NlmSourceFixture&) = delete;
    NlmSourceFixture& operator=(const NlmSourceFixture&) = delete;
    NlmSourceFixture(NlmSourceFixture&&) = delete;
    NlmSourceFixture& operator=(NlmSourceFixture&&) = delete;
    core::Budget& budget;
    image::Raster source;
    std::unique_ptr<color::Interpretation> interpreter;
    image::Plane<double> luminance;
    image::Plane<std::uint8_t> protection;
    image::Plane<double> block;
    exec::Scheduler scheduler;
};
inline core::Result<image::BlockObservation> extract_nlm_source(NlmSourceFixture& fixture) {
    image::BlockObservation observation;
    const auto descriptor = fixture.interpreter->descriptor();
    const bool gray = fixture.interpreter->category() == image::InterpretationCategory::gray1;
    auto reader = fixture.interpreter->reader();
    if (!reader) {
        return std::unexpected(std::move(reader.error()));
    }
    for (std::uint32_t y = 0; y < 32; ++y) {
        const auto block = fixture.block.view().row(0);
        auto read =
            gray ? reader.value().read_gray(y, 0, block) : reader.value().read_rgb(y, 0, block);
        if (!read) {
            return std::unexpected(std::move(read.error()));
        }
        if (fixture.interpreter->descriptor() != descriptor || !image::valid_unit_samples(block)) {
            return core::failure(core::ErrorCode::invariant, "Invalid extraction ownership");
        }
        observation.flattened_pixels += read->flattened_pixels;
        observation.clipped_components += read->clipped_components;
        const auto row = fixture.luminance.view().row(y);
        for (std::uint32_t x = 0; x < 32; ++x) {
            const auto values = block.subspan(std::size_t{x} * (gray ? 1 : 3), gray ? 1 : 3);
            row.subspan(x, 1).front() = gray ? values.front()
                                             : (0.2126 * values.front()) +
                                                   (0.7152 * values.subspan(1).front()) +
                                                   (0.0722 * values.subspan(2).front());
        }
    }
    return observation;
}
inline core::Result<methods::NlmFacts> apply_nlm_fixture(NlmSourceFixture& fixture,
                                                         methods::NlmParameters parameters = {}) {
    return methods::apply(methods::Nlm::create(parameters).value(),
                          {fixture.luminance.view(), fixture.protection.view().as_const(),
                           fixture.budget, fixture.scheduler});
}
} // namespace docenhance::tests
