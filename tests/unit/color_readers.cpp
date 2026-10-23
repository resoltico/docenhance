// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/color/interpretation.hpp"
#include "docenhance/color/profile.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <iostream>
#include <latch>
#include <thread>
#include <utility>
#include <vector>

namespace docenhance::tests {
namespace {
constexpr std::size_t continuous_budget = std::size_t{64} * 1024 * 1024;
using ColorPair = std::array<std::array<double, 6>, 2>;
void read_color_pair(color::InterpretationReader& first, color::InterpretationReader& second,
                     ColorPair& output) {
    std::array<core::Result<image::BlockObservation>, 2> results{};
    std::array<std::exception_ptr, 2> errors{};
    std::latch start{1};
    const auto read = [&](std::size_t index, color::InterpretationReader& reader) {
        start.wait();
        try {
            results.at(index) =
                reader.read_rgb(static_cast<std::uint32_t>(index), 0, output.at(index));
        } catch (...) {
            errors.at(index) = std::current_exception();
        }
    };
    // Declare thread owners last: partial-launch unwind joins before destroying their borrows.
    std::array<std::jthread, 2> threads{};
    try {
        threads.at(0) = std::jthread{[&] { read(0, first); }};
        threads.at(1) = std::jthread{[&] { read(1, second); }};
    } catch (...) {
        start.count_down();
        throw;
    }
    start.count_down();
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    for (const auto& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
    REQUIRE(results.at(0));
    REQUIRE(results.at(1));
}
std::array<double, 6> reference_color_row(const image::Raster& source, image::PngShape output,
                                          core::Budget& budget) {
    const auto interpreter =
        color::Interpretation::create(source, image::InterpretationPolicy(), output, budget);
    REQUIRE(interpreter);
    REQUIRE(interpreter.value()->workers() == 1);
    auto reader = interpreter.value()->reader();
    REQUIRE(reader);
    std::array<double, 6> samples{};
    REQUIRE(reader.value().read_rgb(0, 0, samples));
    return samples;
}
} // namespace
TEST_CASE("Exclusive color readers retain private native scratch through moves and joined work") {
    core::Budget budget{continuous_budget};
    image::Raster source;
    source.shape = {.width = 2, .height = 2, .model = image::SampleModel::rgb};
    source.pixels = image::Plane<std::uint8_t>::allocate(budget, 6, 2).value();
    const std::array<std::uint8_t, 6> samples{64, 128, 192, 16, 128, 240};
    for (std::uint32_t row = 0; row < 2; ++row) {
        std::ranges::copy(samples, source.pixels.view().row(row).begin());
    }
    source.metadata.icc = color::make_output_profile(budget, false).value();
    const auto held = budget.used();
    const image::PngShape output_shape{
        .width = 2,
        .height = 2,
        .model = image::SampleModel::rgb,
        .depth = image::PngDepth(),
    };
    for (const auto workers : {0U, 257U}) {
        const auto refused = color::Interpretation::create(source, image::InterpretationPolicy(),
                                                           output_shape, budget,
                                                           {
                                                               .workers = workers,
                                                               .cancellation = core::Cancellation(),
                                                           });
        REQUIRE(!refused);
        CHECK(refused.error().code == core::ErrorCode::argument);
        CHECK(budget.used() == held);
    }
    {
        auto interpreter = color::Interpretation::create(source, image::InterpretationPolicy(),
                                                         output_shape, budget,
                                                         {
                                                             .workers = 2,
                                                             .cancellation = core::Cancellation(),
                                                         });
        REQUIRE(interpreter);
        REQUIRE(interpreter.value()->workers() == 2);
        const auto descriptor = interpreter.value()->descriptor();
        auto first = interpreter.value()->reader();
        auto second = interpreter.value()->reader();
        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(!first.value().scratch().empty());
        REQUIRE(!second.value().scratch().empty());
        CHECK(first.value().scratch().data() != second.value().scratch().data());
        const auto exhausted = interpreter.value()->reader();
        REQUIRE(!exhausted);
        CHECK(exhausted.error().code == core::ErrorCode::invariant);
        {
            auto moved = std::move(first.value());
            CHECK(moved.ready());
            CHECK(!first.value().ready());
            CHECK(first.value().scratch().empty());
            ColorPair output{};
            CHECK(!first.value().read_rgb(0, 0, output[0]));
            read_color_pair(moved, second.value(), output);
            CHECK(output[0] == output[1]);
            CHECK(image::valid_unit_samples(output[0]));
            const auto inplace = moved.scratch().first(output[0].size());
            REQUIRE(moved.read_rgb(0, 0, inplace));
            CHECK(std::ranges::equal(inplace, output[0]));
            const auto shifted = moved.read_rgb(0, 0, moved.scratch().subspan(1, 6));
            REQUIRE(!shifted);
            CHECK(shifted.error().code == core::ErrorCode::argument);
            CHECK(std::ranges::equal(inplace, output[0]));
            CHECK(interpreter.value()->descriptor() == descriptor);
        }
        const auto reacquired = interpreter.value()->reader();
        REQUIRE(reacquired);
        CHECK(reacquired.value().ready());
    }
    CHECK(budget.used() == held);
}
TEST_CASE("Native ICC reader upper bounds select a healthy pool and preserve serial values") {
    core::Budget budget{std::size_t{64} * 1024 * 1024};
    image::Raster source;
    source.shape = {.width = 2, .height = 64, .model = image::SampleModel::rgb};
    source.pixels = image::Plane<std::uint8_t>::allocate(budget, 6, 64).value();
    for (std::uint32_t row = 0; row < 64; ++row) {
        std::ranges::fill(source.pixels.view().row(row), std::uint8_t{127});
    }
    source.metadata.icc = color::make_output_profile(budget, false).value();
    const auto held = budget.used();
    const image::PngShape output_shape{
        .width = 2,
        .height = 64,
        .model = image::SampleModel::rgb,
        .depth = image::PngDepth(),
    };
    const auto expected = reference_color_row(source, output_shape, budget);
    CHECK(budget.used() == held);
    {
        auto interpreter = color::Interpretation::create(source, image::InterpretationPolicy(),
                                                         output_shape, budget,
                                                         {
                                                             .workers = 64,
                                                             .cancellation = core::Cancellation(),
                                                         });
        REQUIRE(interpreter);
        const auto selected = interpreter.value()->workers();
        REQUIRE(selected >= 1);
        REQUIRE(selected <= 64);
        std::vector<color::InterpretationReader> readers;
        readers.reserve(selected);
        for (unsigned index = 0; index < selected; ++index) {
            auto reader = interpreter.value()->reader();
            REQUIRE(reader);
            readers.emplace_back(std::move(reader.value()));
            CHECK(readers.back().ready());
        }
        const auto exhausted = interpreter.value()->reader();
        REQUIRE(!exhausted);
        CHECK(exhausted.error().code == core::ErrorCode::invariant);
        std::size_t native_reads = 0;
        for (auto& reader : readers) {
            std::array<double, 6> output{};
            REQUIRE(reader.read_rgb(0, 0, output));
            CHECK(image::valid_unit_samples(output));
            CHECK(output == expected);
            ++native_reads;
        }
        std::cout << "ICC64 requested=64 prepared=" << readers.size()
                  << " native_reads=" << native_reads << " peak=" << budget.peak() << '\n';
        CHECK(native_reads == selected);
    }
    CHECK(budget.used() == held);
    std::cout << "ICC64 refunded=" << (budget.used() == held) << " live=" << budget.used() << '\n';
}
TEST_CASE("Measured shared budget pressure selects a smaller healthy native reader pool") {
    core::Budget budget{continuous_budget};
    image::Raster source;
    source.shape = {.width = 2, .height = 64, .model = image::SampleModel::rgb};
    source.pixels = image::Plane<std::uint8_t>::allocate(budget, 6, 64).value();
    for (std::uint32_t row = 0; row < 64; ++row) {
        std::ranges::fill(source.pixels.view().row(row), std::uint8_t{127});
    }
    source.metadata.icc = color::make_output_profile(budget, false).value();
    const auto held = budget.used();
    const image::PngShape output_shape{
        .width = 2,
        .height = 64,
        .model = image::SampleModel::rgb,
        .depth = image::PngDepth(),
    };
    std::size_t one_reader_peak = 0;
    {
        const auto single = color::Interpretation::create(source, image::InterpretationPolicy(),
                                                          output_shape, budget,
                                                          {
                                                              .workers = 1,
                                                              .cancellation = core::Cancellation(),
                                                          });
        REQUIRE(single);
        REQUIRE(single.value()->workers() == 1);
        one_reader_peak = budget.peak() - held;
    }
    REQUIRE(budget.used() == held);
    REQUIRE(one_reader_peak > 0);
    REQUIRE(one_reader_peak < budget.available());
    {
        const auto pressure = budget.reserve(budget.available() - one_reader_peak).value();
        const auto occupied = budget.used();
        {
            auto reduced = color::Interpretation::create(source, image::InterpretationPolicy(),
                                                         output_shape, budget,
                                                         {
                                                             .workers = 64,
                                                             .cancellation = core::Cancellation(),
                                                         });
            REQUIRE(reduced);
            CHECK(reduced.value()->workers() >= 1);
            CHECK(reduced.value()->workers() < 64);
            auto reader = reduced.value()->reader();
            REQUIRE(reader);
            std::array<double, 6> output{};
            REQUIRE(reader.value().read_rgb(0, 0, output));
            CHECK(image::valid_unit_samples(output));
            std::cout << "ICC pressure_readers=" << reduced.value()->workers()
                      << " available_envelope=" << one_reader_peak << '\n';
        }
        CHECK(budget.used() == occupied);
    }
    CHECK(budget.used() == held);
}
} // namespace docenhance::tests
