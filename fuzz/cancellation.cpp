// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"

#include "binary_reference.hpp"
#include "cancellation_probe.hpp"
#include "docenhance/codec/continuous_png.hpp"
#include "docenhance/codec/png.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "docenhance/methods/box_mean.hpp"
#include "png_fixture.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {
namespace core = docenhance::core;
namespace exec = docenhance::exec;
namespace image = docenhance::image;
namespace codec = docenhance::codec;
namespace methods = docenhance::methods;
namespace tests = docenhance::tests;
using docenhance::fuzz::FuzzInput;
using docenhance::fuzz::require;
constexpr std::size_t budget_limit = std::size_t{2} * 1024 * 1024;
constexpr std::uint32_t extent = 8;
constexpr std::uint8_t sentinel = 17;
constexpr std::size_t pixels = static_cast<std::size_t>(extent) * extent;

void outcome(const core::Result<void>& result) {
    if (tests::CheckpointStop::stopped()) {
        require(!result && result.error().code == core::ErrorCode::cancelled,
                "an observed checkpoint produces typed cancellation");
    } else {
        require(result.has_value(), "without observed cancellation the operation completes");
    }
}
template <typename Method>
bool binary_outcome(const Method& method, image::PlaneView<const std::uint16_t> source,
                    image::PlaneView<std::uint8_t> destination, core::Budget& budget,
                    const exec::Scheduler& scheduler) {
    const auto result = methods::binarize(method, {source, destination, budget, scheduler});
    if (tests::CheckpointStop::stopped()) {
        require(!result && result.error().code == core::ErrorCode::cancelled,
                "actual observed Q16 checkpoint produces cancellation and no Facts");
    } else {
        require(result && result->black_samples + result->white_samples == pixels,
                "without observed cancellation actual Q16 classifies the complete frame");
    }
    return result.has_value();
}
void check_binary_samples(image::PlaneView<const std::uint16_t> source,
                          image::PlaneView<const std::uint8_t> destination,
                          std::span<const std::uint16_t> original, bool adaptive, bool complete) {
    const auto method = methods::Sauvola::create({.window = 7}).value();
    constexpr std::uint16_t fixed_split = 32767;
    constexpr std::uint16_t padding = 0x3141;
    for (std::uint32_t row = 0; row < extent; ++row) {
        for (std::uint32_t column = 0; column < extent; ++column) {
            const auto sample = source.row(row).subspan(column, 1).front();
            require(sample == original.subspan((row * extent) + column, 1).front(),
                    "Q16 source samples remain immutable");
            if (complete) {
                const auto fixed = static_cast<std::uint8_t>(sample > fixed_split);
                const auto expected =
                    adaptive ? tests::binary_sauvola_reference(source, method, column, row) : fixed;
                require(destination.row(row).subspan(column, 1).front() == expected,
                        "actual complete logical0/1 output matches independent Q16 classification");
            }
        }
    }
    const auto backing = source.storage();
    for (std::size_t i = 0; i < backing.size(); ++i) {
        if (i % source.row_pitch() >= extent) {
            require(backing.subspan(i, 1).front() == padding,
                    "Q16 source padding remains immutable");
        }
    }
}
void binarize(FuzzInput& input, core::Checkpoint phase, std::size_t after) {
    core::Budget budget{budget_limit};
    auto source = image::Plane<std::uint16_t>::allocate(budget, extent, extent).value();
    auto destination = image::Plane<std::uint8_t>::allocate(budget, extent, extent).value();
    std::array<std::uint8_t, pixels> low{};
    for (auto& value : low) {
        value = input.byte();
    }
    // Keep the actual retained adaptive selector position. Extra bytes extend all Q16 codes;
    // absent extension bytes retain the old Q=257*s input embeddings without an output bridge.
    const bool adaptive = input.byte() % 2U != 0;
    std::array<std::uint16_t, pixels> original{};
    for (std::size_t i = 0; i < original.size(); ++i) {
        original.at(i) = static_cast<std::uint16_t>(
            ((std::uint32_t{low.at(i)} ^ std::uint32_t{input.byte()}) << 8U) |
            std::uint32_t{low.at(i)});
    }
    constexpr std::uint16_t padding = 0x3141;
    std::ranges::fill(source.view().storage(), padding);
    for (std::uint32_t row = 0; row < extent; ++row) {
        std::ranges::copy(
            std::span{original}.subspan(static_cast<std::size_t>(row) * extent, extent),
            source.view().row(row).begin());
    }
    std::ranges::fill(destination.view().storage(), sentinel);
    const auto held = budget.used();
    const tests::CheckpointStop stop{phase, after};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const bool complete =
        adaptive ? binary_outcome(methods::Sauvola::create({.window = 7}).value(),
                                  source.view().as_const(), destination.view(), budget, scheduler)
                 : binary_outcome(methods::FixedThreshold::create().value(),
                                  source.view().as_const(), destination.view(), budget, scheduler);
    require(budget.used() == held, "Q16 cancellation and completion refund all scratch");
    check_binary_samples(source.view().as_const(), destination.view().as_const(), original,
                         adaptive, complete);
}
void box_mean(core::Checkpoint phase, std::size_t after) {
    core::Budget budget{budget_limit};
    auto source = image::Plane<float>::allocate(budget, extent, extent).value();
    auto destination = image::Plane<float>::allocate(budget, extent, extent).value();
    std::ranges::fill(source.view().storage(), 0.75F);
    const auto held = budget.used();
    const tests::CheckpointStop stop{phase, after};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result =
        methods::box_mean(source.view().as_const(), destination.view(), 4, scheduler, budget);
    outcome(result);
    require(budget.used() == held, "box-mean intermediate storage is always refunded");
    if (result) {
        for (std::uint32_t row = 0; row < extent; ++row) {
            require(std::ranges::all_of(destination.view().row(row),
                                        [](float v) { return v == 0.75F; }),
                    "a completed reflected mean preserves a constant plane");
        }
    }
}
void check_decoded(const core::Result<image::Raster>& result, std::uint8_t sample) {
    if (tests::CheckpointStop::stopped()) {
        require(!result && result.error().code == core::ErrorCode::cancelled,
                "codec cancellation is not corrupt-input failure");
        return;
    }
    require(result.has_value(), "uncancelled independent PNG decodes");
    require(result->shape.width == extent && result->shape.height == extent &&
                result->shape.model == image::SampleModel::gray &&
                result->shape.depth == image::SampleDepth::byte(),
            "independent gray fixture retains its actual typed byte raster shape");
    for (std::uint32_t row = 0; row < extent; ++row) {
        require(std::ranges::all_of(result->pixels.view().row(row),
                                    [sample](auto v) { return v == sample; }),
                "uncancelled codec samples are exact");
    }
}
void decode(FuzzInput& input, std::size_t after) {
    const tests::GrayFixture fixture{
        .width = extent,
        .height = extent,
        .depth = 8,
        .interlaced = input.byte() % 2U != 0,
        .filter = input.byte() % 5U,
        .samples = std::vector<std::uint8_t>(pixels, input.byte()),
    };
    const auto bytes = tests::make_gray_png(fixture);
    core::Budget budget{budget_limit};
    const tests::CheckpointStop stop{core::Checkpoint::decode, after};
    const codec::PngLimits limits{.encoded_bytes = 65536, .pixels = 4096};
    {
        const auto result = codec::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded,
                                                     stop.cancellation(), limits);
        check_decoded(result, fixture.samples.front());
    }
    require(budget.used() == 0, "codec allocations are refunded after cancellation or success");
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    FuzzInput input{std::span<const std::uint8_t>{data, size}};
    const auto family = input.byte() % 3U;
    const auto after = input.byte();
    constexpr auto phases = std::to_array<core::Checkpoint>({
        core::Checkpoint::allocation,
        core::Checkpoint::scheduling,
        core::Checkpoint::initialization,
        core::Checkpoint::processing,
    });
    const auto phase = phases.at(input.byte() % phases.size());
    if (family == 0) {
        binarize(input, phase, after);
    } else if (family == 1) {
        box_mean(phase, after);
    } else {
        decode(input, after);
    }
    return 0;
}
