// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/box_mean.hpp"
#include "png_fixture.hpp"
#include "sauvola_reference.hpp"
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
namespace io = docenhance::io;
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
void binarize(FuzzInput& input, core::Checkpoint phase, std::size_t after) {
    core::Budget budget{budget_limit};
    auto source = image::Plane<std::uint8_t>::allocate(budget, extent, extent).value();
    auto destination = image::Plane<std::uint8_t>::allocate(budget, extent, extent).value();
    std::array<std::uint8_t, pixels> original{};
    for (auto& pixel : original) {
        pixel = input.byte();
    }
    for (std::uint32_t row = 0; row < extent; ++row) {
        std::ranges::copy(
            std::span{original}.subspan(static_cast<std::size_t>(row) * extent, extent),
            source.view().row(row).begin());
    }
    std::ranges::fill(destination.view().storage(), sentinel);
    const auto held = budget.used();
    const auto sauvola = methods::Sauvola::create({.window = 7}).value();
    const bool adaptive = input.byte() % 2U != 0;
    const methods::Binarization selected =
        adaptive ? methods::Binarization{sauvola}
                 : methods::Binarization{methods::FixedThreshold::create().value()};
    const tests::CheckpointStop stop{phase, after};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = methods::binarize(source.view().as_const(), destination.view(), selected,
                                          {.scheduler = scheduler, .budget = budget});
    outcome(result);
    require(budget.used() == held, "cancellation and completion refund scratch storage");
    const tests::SauvolaReference reference{
        .source = source.view().as_const(),
        .window = 7,
        .k = sauvola.k(),
        .r = sauvola.r(),
    };
    for (std::uint32_t row = 0; row < extent; ++row) {
        for (std::uint32_t column = 0; column < extent; ++column) {
            const auto sample = source.view().row(row).subspan(column, 1).front();
            require(sample == original.at((row * extent) + column),
                    "source bytes remain immutable");
            if (result) {
                const auto fixed = static_cast<std::uint8_t>(sample <= 127 ? 0 : 255);
                const auto expected = adaptive ? reference.at(column, row) : fixed;
                require(destination.view().row(row).subspan(column, 1).front() == expected,
                        "completed output matches an independent sample definition");
            }
        }
    }
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
void check_decoded(const core::Result<image::Plane<std::uint8_t>>& result, std::uint8_t sample) {
    if (tests::CheckpointStop::stopped()) {
        require(!result && result.error().code == core::ErrorCode::cancelled,
                "codec cancellation is not corrupt-input failure");
        return;
    }
    require(result.has_value(), "uncancelled independent PNG decodes");
    for (std::uint32_t row = 0; row < extent; ++row) {
        require(
            std::ranges::all_of(result->view().row(row), [sample](auto v) { return v == sample; }),
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
    const io::PngLimits limits{.encoded_bytes = 65536, .pixels = 4096};
    {
        const auto result = io::decode_grayscale_png(bytes, budget, limits, stop.cancellation());
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
