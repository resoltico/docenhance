// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "binary_detail.hpp"
#include "binary_input_cases.hpp"
#include "binary_resource_cases.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "otsu_reference.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace docenhance::tests {
namespace {
exec::Scheduler otsu_schedule(unsigned workers, core::Cancellation cancellation = {}) {
    const auto concurrency = exec::Concurrency::resolve(workers, workers, 0, 0);
    REQUIRE(concurrency);
    return exec::Scheduler{*concurrency, std::move(cancellation)};
}
std::uint16_t reference_bin(std::uint8_t sample) {
    return static_cast<std::uint16_t>(std::round(4095.0 * (static_cast<double>(sample) / 255.0)));
}
// Independent direct classes rather than the kernel's rolling histogram moments.
OtsuByteReference reference_otsu(std::span<const std::uint8_t> samples) {
    const bool flat = std::ranges::all_of(samples, [&](auto p) { return p == samples.front(); });
    if (flat) {
        return {.threshold_bin = 2047, .single_bin_fallback = true};
    }
    std::array<double, 4095> scores{};
    std::array<bool, 4095> valid{};
    double best = 0;
    for (std::size_t t = 0; t < scores.size(); ++t) {
        double lower_sum = 0;
        double upper_sum = 0;
        std::size_t lower_count = 0;
        std::size_t upper_count = 0;
        for (const auto p : samples) {
            const auto q = reference_bin(p);
            if (q <= t) {
                lower_sum += q;
                ++lower_count;
            } else {
                upper_sum += q;
                ++upper_count;
            }
        }
        if (lower_count == 0 || upper_count == 0) {
            continue;
        }
        const auto lower = static_cast<double>(lower_count);
        const auto upper = static_cast<double>(upper_count);
        const auto total = static_cast<double>(samples.size());
        const double difference = (lower_sum / lower) - (upper_sum / upper);
        scores.at(t) = (lower / total) * (upper / total) * difference * difference;
        valid.at(t) = true;
        best = std::max(best, scores.at(t));
    }
    for (std::size_t t = 0; t < scores.size(); ++t) {
        if (valid.at(t) && std::abs(best - scores.at(t)) <= 1e-12 * std::max(1.0, best)) {
            return {.threshold_bin = static_cast<std::uint16_t>(t), .single_bin_fallback = false};
        }
    }
    FAIL("Nonflat oracle must have a valid split");
    return {};
}
core::Checkpoint stop_at = core::Checkpoint::commit;
unsigned remaining = 0;
bool otsu_stop(core::Checkpoint checkpoint) noexcept {
    if (checkpoint != stop_at) {
        return false;
    }
    if (remaining == 0) {
        return true;
    }
    --remaining;
    return false;
}
void check_otsu_stop(image::PlaneView<const std::uint16_t> source,
                     image::PlaneView<std::uint8_t> destination, core::Checkpoint checkpoint,
                     unsigned passes) {
    stop_at = checkpoint;
    remaining = passes;
    core::Budget budget{methods::otsu_scratch_bytes + binary_partial_bytes(source.height(), 64)};
    const auto scheduler = otsu_schedule(4, core::Cancellation{{}, otsu_stop});
    std::ranges::fill(destination.storage(), std::uint8_t{42});
    const auto result =
        methods::binarize(methods::Otsu::create(), {source, destination, budget, scheduler});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::cancelled);
    CHECK(budget.used() == 0);
    if (checkpoint != core::Checkpoint::processing || passes == 0) {
        const bool unchanged =
            std::ranges::all_of(destination.storage(), [](auto p) { return p == 42; });
        CHECK(unchanged);
    } else {
        CHECK(destination.row(0).front() == 0);
        CHECK(destination.row(0).subspan(1024, 1).front() == 42);
    }
}
} // namespace
TEST_CASE("Otsu agrees with independent direct classes and worker counts", "[otsu]") {
    static_assert(!std::is_default_constructible_v<methods::Otsu>);
    std::vector<std::vector<std::uint8_t>> fixtures{
        {0, 255}, {85, 170}, {0, 85, 170}, {85, 170, 255}, {0, 0, 1, 2, 3, 254, 255},
        {0},      {127},     {128},        {255},
    };
    for (unsigned seed = 1; seed <= 8; ++seed) {
        std::vector<std::uint8_t> pixels;
        pixels.reserve(113);
        for (unsigned x = 0; x < 113; ++x) {
            pixels.push_back(static_cast<std::uint8_t>(((x * x * 29U) + (seed * x * 71U)) % 256U));
        }
        fixtures.push_back(std::move(pixels));
    }
    for (const auto& pixels : fixtures) {
        const auto expected = reference_otsu(pixels);
        const auto width = static_cast<std::uint32_t>(pixels.size());
        std::vector<std::uint16_t> codes;
        codes.reserve(pixels.size());
        for (const auto p : pixels) {
            codes.push_back(static_cast<std::uint16_t>(257U * p));
        }
        const auto source = image::PlaneView<const std::uint16_t>::create(
            codes, {.width = width, .height = 1, .stride = std::size_t{2} * width});
        std::vector<std::uint8_t> output(pixels.size(), 42);
        const auto destination = image::PlaneView<std::uint8_t>::create(
            output, {.width = width, .height = 1, .stride = width});
        REQUIRE(source);
        REQUIRE(destination);
        for (const auto workers : {1U, 4U, 64U}) {
            core::Budget budget{methods::otsu_scratch_bytes + binary_partial_bytes(1, 64)};
            const auto scheduler = otsu_schedule(workers);
            const auto fitted = methods::binarize(methods::Otsu::create(),
                                                  {*source, *destination, budget, scheduler});
            REQUIRE(fitted);
            CHECK(fitted->threshold_bin == expected.threshold_bin);
            CHECK(fitted->single_bin_fallback == expected.single_bin_fallback);
            CHECK(budget.used() == 0);
            for (std::size_t x = 0; x < pixels.size(); ++x) {
                CHECK(output.at(x) ==
                      (reference_bin(pixels.at(x)) <= expected.threshold_bin ? 0 : 1));
            }
        }
    }
}
TEST_CASE("Otsu chooses earliest nonempty split on equal-score plateaus", "[otsu]") {
    core::Budget budget{methods::otsu_scratch_bytes + binary_partial_bytes(1, 64)};
    const auto scheduler = otsu_schedule(1);
    for (const auto pixels : {
             std::array<std::uint16_t, 3>{0, 21845, 43690},
             std::array<std::uint16_t, 3>{21845, 43690, 65535},
         }) {
        const auto source = image::PlaneView<const std::uint16_t>::create(
            pixels, {.width = 3, .height = 1, .stride = 6});
        std::array<std::uint8_t, 3> output{};
        const auto destination =
            image::PlaneView<std::uint8_t>::create(output, {.width = 3, .height = 1, .stride = 3});
        REQUIRE(source);
        REQUIRE(destination);
        const auto fitted =
            methods::binarize(methods::Otsu::create(), {*source, *destination, budget, scheduler});
        REQUIRE(fitted);
        CHECK(fitted->threshold_bin ==
              reference_bin(static_cast<std::uint8_t>(pixels.front() / 257)));
        CHECK(!fitted->single_bin_fallback);
    }
}

TEST_CASE("Otsu relative tolerance is anchored to the strictly greatest score", "[otsu]") {
    for (const auto count : {1000U, 10000U}) {
        // Independently calculate only the two distinct class partitions. The slight population
        // asymmetry makes the second partition strictly better, without an exact-score tie.
        const double low = count;
        const double high = count + 1;
        const double total = low + 1 + high;
        const double first_upper_mean = (1365.0 + (high * 2730.0)) / (1 + high);
        const double second_lower_mean = 1365.0 / (low + 1);
        const double second_difference = 2730.0 - second_lower_mean;
        const double first_score =
            (low / total) * ((1 + high) / total) * first_upper_mean * first_upper_mean;
        const double second_score =
            ((low + 1) / total) * (high / total) * second_difference * second_difference;
        REQUIRE(second_score > first_score);
        const double gap = second_score - first_score;
        const double tolerance = 1e-12 * second_score;
        const bool within_tolerance = count == 10000;
        CHECK((gap <= tolerance) == within_tolerance);
        std::vector<std::uint16_t> pixels(count, 0);
        pixels.push_back(21845);
        pixels.insert(pixels.end(), count + 1, 43690);
        const auto width = static_cast<std::uint32_t>(pixels.size());
        const auto source = image::PlaneView<const std::uint16_t>::create(
            pixels, {.width = width, .height = 1, .stride = std::size_t{2} * width});
        std::vector<std::uint8_t> output(pixels.size(), 42);
        const auto destination = image::PlaneView<std::uint8_t>::create(
            output, {.width = width, .height = 1, .stride = width});
        REQUIRE(source);
        REQUIRE(destination);
        core::Budget budget{methods::otsu_scratch_bytes + binary_partial_bytes(1, 64)};
        const auto scheduler = otsu_schedule(1);
        const auto fitted =
            methods::binarize(methods::Otsu::create(), {*source, *destination, budget, scheduler});
        REQUIRE(fitted);
        CHECK(fitted->threshold_bin == (within_tolerance ? 0 : 1365));
        CHECK(!fitted->single_bin_fallback);
    }
}
TEST_CASE("Otsu uses every byte quantization boundary and ignores row padding", "[otsu]") {
    std::array<std::uint16_t, 516> pixels{};
    pixels.fill(65535);
    for (std::size_t p = 0; p < 256; ++p) {
        pixels.at(p) = static_cast<std::uint16_t>(257 * p);
        pixels.at(258 + p) = static_cast<std::uint16_t>(257 * (255 - p));
    }
    const auto before = pixels;
    std::array<std::uint8_t, 516> output{};
    output.fill(42);
    const auto source = image::PlaneView<const std::uint16_t>::create(
        pixels, {.width = 256, .height = 2, .stride = 516});
    const auto destination =
        image::PlaneView<std::uint8_t>::create(output, {.width = 256, .height = 2, .stride = 258});
    REQUIRE(source);
    REQUIRE(destination);
    core::Budget budget{binary_partial_bytes(2, 64)};
    const auto scheduler = otsu_schedule(1);
    const methods::BinaryInput input{*source, *destination, budget, scheduler};
    REQUIRE(methods::validate_binary_input(input));
    auto counts = methods::binary_counts(input, methods::BinaryPartition::rows);
    REQUIRE(counts);
    const auto held = budget.used();
    for (std::uint16_t t = 0; t <= 4094; ++t) {
        const auto classified = methods::binary_rows(input, counts->view(), [t](std::uint16_t p) {
            return methods::binary_otsu_bin(p) <= t;
        });
        REQUIRE(classified);
        std::uint64_t black = 0;
        for (std::size_t p = 0; p < 256; ++p) {
            const bool expected = reference_bin(static_cast<std::uint8_t>(p)) <= t;
            CHECK(output.at(p) == (expected ? 0 : 1));
            CHECK(output.at(258 + 255 - p) == (expected ? 0 : 1));
            black += 2 * static_cast<std::uint64_t>(expected);
        }
        CHECK(classified->black_samples == black);
        CHECK(classified->white_samples == 512 - black);
        for (const auto at : {256U, 257U, 514U, 515U}) {
            CHECK(output.at(at) == 42);
        }
        CHECK(pixels == before);
        CHECK(budget.used() == held);
    }
    *counts = image::Plane<std::uint64_t>{};
    CHECK(budget.used() == 0);
    core::Budget fit_budget{methods::otsu_scratch_bytes + binary_partial_bytes(2, 64)};
    const auto fitted =
        methods::binarize(methods::Otsu::create(), {*source, *destination, fit_budget, scheduler});
    REQUIRE(fitted);
    std::array<std::uint8_t, 256> ramp{};
    for (std::size_t p = 0; p < ramp.size(); ++p) {
        ramp.at(p) = static_cast<std::uint8_t>(p);
    }
    const auto expected = reference_otsu(ramp);
    CHECK(fitted->threshold_bin == expected.threshold_bin);
    CHECK(fitted->single_bin_fallback == expected.single_bin_fallback);
    CHECK(fit_budget.used() == 0);
    CHECK(pixels == before);
}
TEST_CASE("Otsu actual fit reaches every admitted threshold through adjacent bin centers",
          "[otsu]") {
    std::array<std::uint16_t, 2> pixels{};
    std::array<std::uint8_t, 2> output{};
    const auto source = image::PlaneView<const std::uint16_t>::create(
        pixels, {.width = 2, .height = 1, .stride = 4});
    const auto destination =
        image::PlaneView<std::uint8_t>::create(output, {.width = 2, .height = 1, .stride = 2});
    REQUIRE(source);
    REQUIRE(destination);
    const auto scheduler = otsu_schedule(1);
    core::Budget budget{methods::otsu_scratch_bytes + binary_partial_bytes(1, 64)};
    for (std::uint32_t t = 0; t <= 4094; ++t) {
        pixels.front() = static_cast<std::uint16_t>(std::round(65535.0 * t / 4095.0));
        pixels.back() = static_cast<std::uint16_t>(std::round(65535.0 * (t + 1) / 4095.0));
        const auto before = pixels;
        const auto fitted =
            methods::binarize(methods::Otsu::create(), {*source, *destination, budget, scheduler});
        REQUIRE(fitted);
        CHECK(fitted->threshold_bin == t);
        CHECK(!fitted->single_bin_fallback);
        CHECK(fitted->black_samples == 1);
        CHECK(fitted->white_samples == 1);
        CHECK(output == std::array<std::uint8_t, 2>{0, 1});
        CHECK(pixels == before);
        CHECK(budget.used() == 0);
    }
}
TEST_CASE("Otsu validates and reserves histogram and counts before touching output", "[otsu]") {
    std::array<std::uint16_t, 2> pixels{0, 65535};
    std::array<std::uint8_t, 2> output{42, 42};
    const auto source = image::PlaneView<const std::uint16_t>::create(
        pixels, {.width = 2, .height = 1, .stride = 4});
    const auto destination =
        image::PlaneView<std::uint8_t>::create(output, {.width = 2, .height = 1, .stride = 2});
    REQUIRE(source);
    REQUIRE(destination);
    const auto scheduler = otsu_schedule(4);
    const auto needed = methods::otsu_scratch_bytes + binary_partial_bytes(1, 64);
    core::Budget refused{needed - 1};
    const auto result =
        methods::binarize(methods::Otsu::create(), {*source, *destination, refused, scheduler});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::resource);
    CHECK(refused.used() == 0);
    CHECK(output == std::array<std::uint8_t, 2>{42, 42});
    CHECK(!methods::binarize(methods::Otsu::create(), {{}, *destination, refused, scheduler}));
    binary_input_alias_cases();
    methods::OtsuFacts facts{};
    facts.threshold_bin = 1;
    facts.black_samples = facts.white_samples = 1;
    REQUIRE(methods::validate(facts));
    facts.threshold_bin = 4095;
    CHECK(!methods::validate(facts));
    facts.threshold_bin = 2047;
    facts.single_bin_fallback = true;
    facts.black_samples = 2;
    facts.white_samples = 0;
    REQUIRE(methods::validate(facts));
    facts.threshold_bin = 0;
    CHECK(!methods::validate(facts));
    CHECK(output == std::array<std::uint8_t, 2>{42, 42});
    core::Budget exact{needed};
    const auto healthy =
        methods::binarize(methods::Otsu::create(), {*source, *destination, exact, scheduler});
    REQUIRE(healthy);
    CHECK(exact.used() == 0);
    CHECK(exact.peak() == needed);
    CHECK(methods::otsu_scratch_bytes == 32768);
}
TEST_CASE("Otsu cancellation stops bounded phases and refunds scratch", "[otsu]") {
    std::vector<std::uint16_t> pixels(4097, 65535);
    pixels.front() = 0;
    const auto before = pixels;
    std::vector<std::uint8_t> output(pixels.size(), 42);
    const auto source = image::PlaneView<const std::uint16_t>::create(
        pixels, {.width = 4097, .height = 1, .stride = 8194});
    const auto destination = image::PlaneView<std::uint8_t>::create(
        output, {.width = 4097, .height = 1, .stride = 4097});
    REQUIRE(source);
    REQUIRE(destination);
    for (const auto checkpoint : {
             core::Checkpoint::allocation,
             core::Checkpoint::initialization,
             core::Checkpoint::measurement,
             core::Checkpoint::solving,
             core::Checkpoint::processing,
         }) {
        for (const auto passes : {0U, 1U}) {
            check_otsu_stop(*source, *destination, checkpoint, passes);
            CHECK(pixels == before);
        }
    }
    stop_at = core::Checkpoint::commit;
}
} // namespace docenhance::tests
