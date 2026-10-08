// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/denoising.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/opencv/nlm.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <utility>
namespace docenhance::tests {
namespace {
constexpr std::size_t denoise_budget = std::size_t{32} * 1024 * 1024;
constexpr std::uint32_t seam_width = 270;
constexpr std::uint32_t seam_height = 267;
constexpr std::uint32_t pattern_modulus = 65536;
methods::Nlm method(std::uint32_t patch = 3, std::uint32_t search = 7) {
    return methods::Nlm::create({.patch = patch, .search = search}).value();
}
} // namespace
TEST_CASE("NLM correction retains entering doubles and uses a signed native difference") {
    const image::Rgb between{0.123456789, 0.345678912, 0.789123456};
    const auto q = methods::nlm_quantize(between).value();
    REQUIRE(methods::nlm_correct(between, q, q, methods::denoising_default_blend).value() ==
            between);
    REQUIRE(methods::nlm_correct(between, q, 0, 0).value() == between);
    constexpr double maximum = 65535;
    const auto f = image::srgb_encode(image::luminance(between).value()).value();
    constexpr std::uint16_t correction = 13;
    const auto expected =
        image::srgb_decode(f - (static_cast<double>(correction) / maximum)).value();
    const auto corrected =
        methods::nlm_correct(between, q, static_cast<std::uint16_t>(q - correction), 1).value();
    constexpr double tolerance = 1e-14;
    REQUIRE(std::abs(image::luminance(corrected).value() - expected) < tolerance);
    REQUIRE(methods::nlm_correct({0, 0, 0}, 0, UINT16_MAX, 1).value() == image::Rgb{1, 1, 1});
    REQUIRE(methods::nlm_correct({1, 1, 1}, UINT16_MAX, 0, 1).value() == image::Rgb{0, 0, 0});
}
TEST_CASE("Native reservations refund, move and retain ledger lifetime") {
    core::Reservation held;
    constexpr std::size_t limit = 1000;
    {
        core::Budget budget{limit};
        auto reservation = budget.reserve(limit);
        REQUIRE(reservation);
        REQUIRE(budget.used() == limit);
        REQUIRE(!(budget.allocate(1)));
        held = std::move(*reservation);
        REQUIRE(reservation->size() == 0);
        REQUIRE(budget.used() == limit);
    }
    REQUIRE(held.size() == limit);
    held = {};
    REQUIRE(held.size() == 0);
}
TEST_CASE("NLM global halos agree exactly with whole native output at seams and corners") {
    core::Budget budget{denoise_budget};
    const core::Cancellation cancellation;
    auto input = image::Plane<std::uint16_t>::allocate(budget, seam_width, seam_height).value();
    auto tiled = image::Plane<std::uint16_t>::allocate(budget, seam_width, seam_height).value();
    auto whole = image::Plane<std::uint16_t>::allocate(budget, seam_width, seam_height).value();
    constexpr std::uint32_t multiplier = 167;
    constexpr std::uint32_t vertical = 911;
    for (std::uint32_t y = 0; y < seam_height; ++y) {
        for (std::uint32_t x = 0; x < seam_width; ++x) {
            input.view().row(y).subspan(x, 1).front() = static_cast<std::uint16_t>(
                ((x * multiplier) + (y * vertical) + (x * y)) % pattern_modulus);
        }
    }
    for (const auto p : std::array{methods::nlm_min_patch, methods::nlm_max_patch}) {
        for (const auto s : std::array{methods::nlm_default_search, methods::nlm_max_search}) {
            const auto options = method(p, s);
            methods::DenoisingReport report;
            REQUIRE(opencv::denoise(input.view().as_const(), tiled.view(),
                                    {.method = options,
                                     .budget = budget,
                                     .cancellation = cancellation,
                                     .report = report}));
            REQUIRE(opencv::native_tile(input.view().as_const(), whole.view(), options, budget));
            for (std::uint32_t y = 0; y < seam_height; ++y) {
                REQUIRE(std::ranges::equal(tiled.view().row(y), whole.view().row(y)));
            }
            constexpr std::uint64_t calls = 4;
            REQUIRE(report.native_calls == calls);
        }
    }
}
TEST_CASE("Native refusal and deterministic cancellation release all temporary storage") {
    const auto options = method();
    core::Budget budget{denoise_budget};
    const core::Cancellation cancellation;
    constexpr std::uint32_t side = 32;
    auto input = image::Plane<std::uint16_t>::allocate(budget, side, side).value();
    auto output = image::Plane<std::uint16_t>::allocate(budget, side, side).value();
    const auto live = budget.used();
    const auto scratch =
        methods::nlm_native_scratch({.width = side, .height = side}, options).value();
    core::Budget exact_budget{scratch};
    const auto completed =
        opencv::native_tile(input.view().as_const(), output.view(), options, exact_budget);
    REQUIRE(completed);
    REQUIRE(completed->reserved_bytes == scratch);
    REQUIRE(completed->charged_bytes == scratch);
    REQUIRE(exact_budget.used() == 0);
    core::Budget short_budget{scratch - 1};
    REQUIRE(!(opencv::native_tile(input.view().as_const(), output.view(), options, short_budget)));
    REQUIRE(short_budget.used() == 0);
    const CheckpointStop stopped{core::Checkpoint::processing, 1};
    methods::DenoisingReport report;
    const auto stopped_cancellation = stopped.cancellation();
    REQUIRE(!(opencv::denoise(input.view().as_const(), output.view(),
                              {.method = options,
                               .budget = budget,
                               .cancellation = stopped_cancellation,
                               .report = report})));
    REQUIRE(report.native_calls == 0);
    REQUIRE(budget.used() == live);
    REQUIRE(!(opencv::native_tile(input.view().as_const(), input.view(), options, budget)));
}

TEST_CASE("Concurrent native NLM calls own independent scratch and preserve shared input") {
    core::Budget budget{denoise_budget};
    constexpr std::uint32_t side = 32;
    auto input = image::Plane<std::uint16_t>::allocate(budget, side, side).value();
    auto first = image::Plane<std::uint16_t>::allocate(budget, side, side).value();
    auto second = image::Plane<std::uint16_t>::allocate(budget, side, side).value();
    constexpr std::uint16_t sample = 12345;
    std::ranges::fill(input.view().storage(), sample);
    const auto options = method();
    const auto live = budget.used();
    auto worker = std::async(std::launch::async, [&] {
        return opencv::native_tile(input.view().as_const(), first.view(), options, budget);
    });
    REQUIRE(opencv::native_tile(input.view().as_const(), second.view(), options, budget));
    REQUIRE(worker.get());
    REQUIRE(budget.used() == live);
    for (std::uint32_t y = 0; y < side; ++y) {
        REQUIRE(std::ranges::equal(first.view().row(y), second.view().row(y)));
        const auto unchanged =
            std::ranges::all_of(input.view().row(y), [](auto value) { return value == sample; });
        REQUIRE(unchanged);
    }
}
TEST_CASE("Cancellation after native completion reports completed calls and refunds reservation") {
    core::Budget budget{denoise_budget};
    constexpr std::uint32_t side = 32;
    auto input = image::Plane<std::uint16_t>::allocate(budget, side, side).value();
    auto output = image::Plane<std::uint16_t>::allocate(budget, side, side).value();
    std::ranges::fill(input.view().storage(), 0);
    const auto options = method();
    const auto radius = (options.parameters().patch + options.parameters().search - 2) / 2;
    const CheckpointStop stop{core::Checkpoint::processing, side + (2 * radius)};
    const auto cancellation = stop.cancellation();
    methods::DenoisingReport report;
    const auto live = budget.used();
    REQUIRE(!opencv::denoise(
        input.view().as_const(), output.view(),
        {.method = options, .budget = budget, .cancellation = cancellation, .report = report}));
    REQUIRE(report.native_calls == 1);
    REQUIRE(report.native_reserved_peak > 0);
    REQUIRE(budget.used() == live);
}
} // namespace docenhance::tests

namespace docenhance::tests {
TEST_CASE("Completed D01 observations cannot exceed the continuous charged-buffer ceiling") {
    const auto method = methods::Nlm::create().value();
    methods::DenoisingReport report{
        .status = methods::DenoiseStatus::applied,
        .complete = true,
        .requested = method.parameters(),
        .native_h = methods::nlm_native_strength(method.parameters()),
        .eligible_samples = 1,
        .evaluated_samples = 1,
        .corrected_samples = 1,
        .changed_samples = 1,
        .native_calls = 1,
        .native_reserved_peak = 1,
        .preparation_charge_peak = 1073741824,
    };
    CHECK(methods::valid_denoising(report, methods::Denoising{method}));
    ++report.preparation_charge_peak;
    CHECK(!methods::valid_denoising(report, methods::Denoising{method}));
}
} // namespace docenhance::tests
