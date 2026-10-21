// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/codec/continuous_png.hpp"
#include "docenhance/color/interpretation.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "png_depth_fixture.hpp"
#include "png_fixture.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace docenhance::tests {
namespace {
constexpr std::size_t continuous_budget = std::size_t{32} * 1024 * 1024;
constexpr std::uint32_t ramp_samples = 65536;
image::Raster ramp(core::Budget& budget) {
    image::Raster source;
    source.shape = {.width = ramp_samples, .height = 1, .depth = image::SampleDepth::word()};
    const auto bytes = image::raster_row_bytes(source.shape).value();
    source.pixels = image::Plane<std::uint8_t>::allocate(budget, bytes, 1).value();
    const auto row = source.pixels.view().row(0);
    for (std::uint32_t i = 0; i < ramp_samples; ++i) {
        image::write_sample(row.subspan(std::size_t{i} * 2, 2), image::SampleDepth::word(),
                            static_cast<std::uint16_t>(i));
    }
    return source;
}
std::vector<std::uint8_t> continuous_fixture() {
    return make_gray_png({
        .width = 8,
        .height = 8,
        .depth = 8,
        .interlaced = true,
        .samples = std::vector<std::uint8_t>(64, 127),
    });
}
} // namespace
TEST_CASE("Every representation shares interpretation geometry and photometric admission",
          "[continuous][app]") {
    app::Invocation invocation;
    invocation.command = app::Command::process;
    invocation.operands = {"input.png"};
    invocation.option(app::OptionId::out_dir) = "output";
    const auto admitted = app::prepare_process(invocation);
    REQUIRE(admitted);
    REQUIRE(std::holds_alternative<methods::ContinuousOutput>(admitted->plan().output));
    CHECK(std::get<methods::ContinuousOutput>(admitted->plan().output).mode ==
          image::ToneMode::preserve);
    invocation.option(app::OptionId::binarize) = "fixed";
    CHECK(!app::prepare_process(invocation));
    invocation.option(app::OptionId::output_mode) = "bw";
    invocation.option(app::OptionId::illumination) = "morph";
    invocation.option(app::OptionId::denoise) = "tvl1";
    invocation.option(app::OptionId::alpha) = "black";
    invocation.option(app::OptionId::profile_policy) = "srgb";
    invocation.option(app::OptionId::protect_mask) = "mask.png";
    auto binary = app::prepare_process(invocation);
    REQUIRE(binary);
    CHECK(std::holds_alternative<methods::BinaryOutput>(binary->plan().output));
    CHECK(std::holds_alternative<methods::Morphology>(binary->plan().photometric.illumination));
    CHECK(std::holds_alternative<methods::Tvl1>(binary->plan().photometric.denoising));
    CHECK(binary->plan().alpha == image::AlphaPolicy::black);
    CHECK(binary->plan().profile == image::ProfilePolicy::srgb);
    CHECK(binary->mask_path() == "mask.png");
    invocation.option(app::OptionId::bit_depth) = "auto";
    CHECK(!app::prepare_process(invocation));
}
TEST_CASE("Kernel paths have exact request roles and move without entering method parameters",
          "[app][resource]") {
    app::Invocation invocation;
    invocation.command = app::Command::process;
    invocation.operands = {"input.png"};
    invocation.option(app::OptionId::out_dir) = "output";
    invocation.option(app::OptionId::protect_mask) = "mask.png";
    invocation.option(app::OptionId::deblur) = "wiener";
    invocation.option(app::OptionId::psf) = "kernel";
    CHECK(!app::prepare_process(invocation));
    invocation.option(app::OptionId::psf_file) = "private/kernel.png";
    auto admitted = app::prepare_process(invocation);
    REQUIRE(admitted);
    CHECK(admitted->ready());
    CHECK(admitted->psf_path() == "private/kernel.png");
    CHECK(std::holds_alternative<methods::Kernel>(
        std::get<methods::Wiener>(admitted->plan().photometric.restoration).parameters().psf));
    const auto moved = std::move(*admitted);
    CHECK(moved.ready());
    CHECK(!admitted->ready());
    CHECK(admitted->input().empty());
    CHECK(!admitted->mask_path());
    CHECK(!admitted->psf_path());
    CHECK(moved.psf_path() == "private/kernel.png");
    invocation.option(app::OptionId::psf_file) = std::string("bad\xff");
    CHECK(!app::prepare_process(invocation));
    invocation.option(app::OptionId::psf_file) = "private/kernel.png";
    invocation.option(app::OptionId::psf) = "gaussian";
    CHECK(!app::prepare_process(invocation));
}
TEST_CASE("Every 16-bit gray level retains scalar binary64 interpretation",
          "[continuous][precision]") {
    core::Budget budget{continuous_budget};
    const auto source = ramp(budget);
    const auto held = budget.used();
    {
        const image::InterpretationPolicy policy{};
        auto producer = color::Interpretation::create(
            source, policy,
            {.width = ramp_samples, .height = 1, .depth = docenhance::tests::fixture_png_depth(16)},
            budget);
        REQUIRE(producer);
        CHECK((*producer)->category() == image::InterpretationCategory::gray1);
        CHECK((*producer)->descriptor().decoded == source.shape);
        auto row = image::Plane<double>::allocate(budget, image::linear_block_pixels, 1).value();
        auto reader = producer.value()->reader();
        REQUIRE(reader);
        for (std::uint32_t first = 0; first < ramp_samples; first += image::linear_block_pixels) {
            const auto block = row.view().row(0);
            const auto observed = reader.value().read_gray(0, first, block);
            REQUIRE(observed);
            for (std::size_t i = 0; i < block.size(); ++i) {
                const auto encoded = static_cast<double>(first + i) / 65535.0;
                const auto expected =
                    encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
                CHECK(block.subspan(i, 1).front() == expected);
                CHECK(std::floor((65535 * image::srgb_encode(block.subspan(i, 1).front())) + 0.5) ==
                      static_cast<double>(first + i));
            }
            const auto repeated = reader.value().read_gray(0, first, block);
            REQUIRE(repeated);
            CHECK(*repeated == *observed);
            CHECK((*producer)->descriptor().flattened_pixels == 0);
            CHECK((*producer)->descriptor().clipped_components == 0);
        }
    }
    CHECK(budget.used() == held);
}
TEST_CASE("Failed and cancelled color preparation refunds its native allocations",
          "[continuous][resource]") {
    core::Budget source_budget{continuous_budget};
    auto const source = ramp(source_budget);
    const image::InterpretationPolicy policy{};
    constexpr auto ceilings = std::to_array<std::size_t>({0, 1024, 65536});
    for (const auto ceiling : ceilings) {
        core::Budget budget{ceiling};
        {
            const auto result =
                color::Interpretation::create(source, policy,
                                              {
                                                  .width = ramp_samples,
                                                  .height = 1,
                                                  .depth = docenhance::tests::fixture_png_depth(16),
                                              },
                                              budget);
            REQUIRE(!result);
            CHECK(result.error().code == core::ErrorCode::resource);
        }
        CHECK(budget.used() == 0);
    }
    constexpr std::size_t profile_budget = 262144;
    core::Budget compact{profile_budget};
    {
        const auto result = color::Interpretation::create(
            source, policy,
            {.width = ramp_samples, .height = 1, .depth = docenhance::tests::fixture_png_depth(16)},
            compact);
        REQUIRE(result);
    }
    CHECK(compact.used() == 0);
    core::Budget budget{continuous_budget};
    const CheckpointStop stop{core::Checkpoint::allocation, 0};
    const auto result = color::Interpretation::create(
        source, policy,
        {.width = ramp_samples, .height = 1, .depth = docenhance::tests::fixture_png_depth(16)},
        budget, {.cancellation = stop.cancellation()});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::cancelled);
    CHECK(budget.used() == 0);
}
TEST_CASE("Continuous PNG decoding observes cancellation and releases storage",
          "[continuous][png]") {
    const auto bytes = continuous_fixture();
    constexpr std::size_t attempts = 512;
    bool completed = false;
    for (std::size_t after = 0; after < attempts; ++after) {
        core::Budget budget{continuous_budget};
        const CheckpointStop stop{core::Checkpoint::decode, after};
        {
            const auto result = codec::decode_png_raster(
                bytes, budget, image::ProfilePolicy::embedded, stop.cancellation());
            if (CheckpointStop::stopped()) {
                REQUIRE(!result);
                CHECK(result.error().code == core::ErrorCode::cancelled);
            } else {
                REQUIRE(result);
                completed = true;
            }
        }
        CHECK(budget.used() == 0);
        if (completed) {
            break;
        }
    }
    REQUIRE(completed);
}
TEST_CASE("Invalid raw raster descriptors cannot enter color conversion",
          "[continuous][admission]") {
    core::Budget budget{continuous_budget};
    image::Raster source;
    const image::PngShape output{
        .width = ramp_samples,
        .height = 1,
        .depth = docenhance::tests::fixture_png_depth(16),
    };
    CHECK(!color::Interpretation::create(source, {}, output, budget));
    source = ramp(budget);
    source.shape.width = 1;
    CHECK(!color::Interpretation::create(source, {}, output, budget));
    source.shape.width = ramp_samples;
    CHECK(!image::Orientation::from_code(0));
    CHECK(!image::SampleDepth::from_bits(12));
    // A fixed-underlying enum deliberately carries an unnamed value to test factory rejection.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto invalid_depth = static_cast<image::OutputDepth>(255);
    methods::Plan invalid_plan{};
    invalid_plan.output = methods::ContinuousOutput{.depth = invalid_depth};
    CHECK(!methods::validate(invalid_plan));
}
} // namespace docenhance::tests
