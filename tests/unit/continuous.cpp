// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/image/continuous.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/color/converter.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "png_fixture.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

namespace docenhance::tests {
namespace {
constexpr std::size_t continuous_budget = std::size_t{32} * 1024 * 1024;
constexpr std::uint32_t ramp_samples = 65536;
image::Raster ramp(core::Budget& budget) {
    image::Raster source;
    source.shape = {.width = ramp_samples, .height = 1, .depth = image::word_bits};
    const auto bytes = image::raster_row_bytes(source.shape).value();
    source.pixels = image::Plane<std::uint8_t>::allocate(budget, bytes, 1).value();
    const auto row = source.pixels.view().row(0);
    for (std::uint32_t i = 0; i < ramp_samples; ++i) {
        image::write_sample(row.subspan(std::size_t{i} * 2, 2), image::word_bits,
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
TEST_CASE("Continuous admission is distinct from binary method admission", "[continuous][app]") {
    static_assert(!std::is_default_constructible_v<image::Continuous>);
    contract::Invocation invocation{
        .command = contract::Command::process,
        .subject = "input.png",
        .output_directory = "output",
    };
    const auto admitted = app::prepare_process(invocation);
    REQUIRE(admitted);
    REQUIRE(std::holds_alternative<image::Continuous>(admitted->operation()));
    CHECK(std::get<image::Continuous>(admitted->operation()).parameters().mode ==
          image::ToneMode::preserve);
    invocation.binarize = "fixed";
    CHECK(!app::prepare_process(invocation));
    invocation.output_mode = "bw";
    CHECK(app::prepare_process(invocation));
    invocation.bit_depth = "auto";
    CHECK(!app::prepare_process(invocation));
}
TEST_CASE("Every 16-bit gray level survives the no-filter sRGB path", "[continuous][precision]") {
    core::Budget budget{continuous_budget};
    auto source = ramp(budget);
    const auto held = budget.used();
    {
        auto producer =
            color::Converter::create(source, image::Continuous::create({}).value(), budget);
        REQUIRE(producer);
        const auto descriptor = (*producer)->descriptor();
        CHECK(descriptor.shape == source.shape);
        auto row = image::Plane<std::uint8_t>::allocate(budget, source.pixels.width(), 1).value();
        REQUIRE((*producer)->row(0, row.view().row(0), image::RowUse::output));
        CHECK(std::ranges::equal(row.view().row(0), source.pixels.view().row(0)));
        const auto before = (*producer)->report();
        REQUIRE((*producer)->row(0, row.view().row(0), image::RowUse::verification));
        CHECK((*producer)->report().clipped_components == before.clipped_components);
        CHECK((*producer)->report().flattened_pixels == before.flattened_pixels);
    }
    CHECK(budget.used() == held);
}
TEST_CASE("Failed and cancelled color preparation refunds its native allocations",
          "[continuous][resource]") {
    core::Budget source_budget{continuous_budget};
    auto const source = ramp(source_budget);
    constexpr auto ceilings = std::to_array<std::size_t>({0, 1024, 65536, 262144});
    for (const auto ceiling : ceilings) {
        core::Budget budget{ceiling};
        {
            const auto result =
                color::Converter::create(source, image::Continuous::create({}).value(), budget);
            REQUIRE(!result);
            CHECK(result.error().code == core::ErrorCode::resource);
        }
        CHECK(budget.used() == 0);
    }
    core::Budget budget{continuous_budget};
    const CheckpointStop stop{core::Checkpoint::allocation, 0};
    const auto result = color::Converter::create(source, image::Continuous::create({}).value(),
                                                 budget, stop.cancellation());
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
            const auto result = io::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded,
                                                      stop.cancellation());
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
    const auto operation = image::Continuous::create({}).value();
    CHECK(!color::Converter::create(source, operation, budget));
    source = ramp(budget);
    source.shape.width = 1;
    CHECK(!color::Converter::create(source, operation, budget));
    source.shape.width = ramp_samples;
    source.metadata.orientation = 0;
    CHECK(!color::Converter::create(source, operation, budget));
    // A fixed-underlying enum deliberately carries an unnamed value to test factory rejection.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    CHECK(!image::Continuous::create({.depth = static_cast<image::OutputDepth>(255)}));
}
} // namespace docenhance::tests
