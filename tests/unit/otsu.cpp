// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/otsu.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"

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
    return exec::Scheduler{exec::Concurrency::resolve(workers, workers, 0, 0).value(),
                           std::move(cancellation)};
}
std::uint16_t reference_bin(std::uint8_t sample) {
    return static_cast<std::uint16_t>(std::round(4095.0 * (static_cast<double>(sample) / 255.0)));
}
// Independent direct classes rather than the kernel's rolling histogram moments.
methods::OtsuObservation reference_otsu(std::span<const std::uint8_t> samples) {
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
void check_otsu_stop(image::PlaneView<const std::uint8_t> source,
                     image::PlaneView<std::uint8_t> destination, core::Checkpoint checkpoint,
                     unsigned passes) {
    stop_at = checkpoint;
    remaining = passes;
    core::Budget budget{methods::otsu_scratch_bytes};
    const auto scheduler = otsu_schedule(4, core::Cancellation{{}, otsu_stop});
    std::ranges::fill(destination.storage(), 42);
    const auto result =
        methods::otsu(source, destination, {.scheduler = scheduler, .budget = budget});
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
        const auto source = image::PlaneView<const std::uint8_t>::create(
                                pixels, {.width = width, .height = 1, .stride = width})
                                .value();
        std::vector<std::uint8_t> output(pixels.size(), 42);
        const auto destination = image::PlaneView<std::uint8_t>::create(
                                     output, {.width = width, .height = 1, .stride = width})
                                     .value();
        for (const auto workers : {1U, 4U, 64U}) {
            core::Budget budget{methods::otsu_scratch_bytes};
            const auto scheduler = otsu_schedule(workers);
            const auto fitted =
                methods::otsu(source, destination, {.scheduler = scheduler, .budget = budget});
            REQUIRE(fitted);
            CHECK(*fitted == expected);
            CHECK(budget.used() == 0);
            for (std::size_t x = 0; x < pixels.size(); ++x) {
                CHECK(output.at(x) ==
                      (reference_bin(pixels.at(x)) <= expected.threshold_bin ? 0 : 255));
            }
        }
    }
}
TEST_CASE("Otsu chooses earliest nonempty split on equal-score plateaus", "[otsu]") {
    core::Budget budget{methods::otsu_scratch_bytes};
    for (const auto pixels :
         {std::array<std::uint8_t, 3>{0, 85, 170}, std::array<std::uint8_t, 3>{85, 170, 255}}) {
        const auto source = image::PlaneView<const std::uint8_t>::create(
                                pixels, {.width = 3, .height = 1, .stride = 3})
                                .value();
        const auto fitted = methods::fit_otsu(source, budget);
        REQUIRE(fitted);
        CHECK(fitted->threshold_bin == reference_bin(pixels.front()));
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
        std::vector<std::uint8_t> pixels(count, 0);
        pixels.push_back(85);
        pixels.insert(pixels.end(), count + 1, 170);
        const auto width = static_cast<std::uint32_t>(pixels.size());
        const auto source = image::PlaneView<const std::uint8_t>::create(
                                pixels, {.width = width, .height = 1, .stride = width})
                                .value();
        core::Budget budget{methods::otsu_scratch_bytes};
        const auto fitted = methods::fit_otsu(source, budget);
        REQUIRE(fitted);
        CHECK(fitted->threshold_bin == (within_tolerance ? 0 : 1365));
        CHECK(!fitted->single_bin_fallback);
    }
}
TEST_CASE("Otsu uses every byte quantization boundary and ignores row padding", "[otsu]") {
    std::array<std::uint8_t, 516> pixels{};
    std::ranges::fill(pixels, 255);
    for (std::size_t p = 0; p < 256; ++p) {
        pixels.at(p) = static_cast<std::uint8_t>(p);
        pixels.at(258 + p) = static_cast<std::uint8_t>(255 - p);
    }
    std::array<std::uint8_t, 516> output{};
    std::ranges::fill(output, 42);
    const auto source = image::PlaneView<const std::uint8_t>::create(
                            pixels, {.width = 256, .height = 2, .stride = 258})
                            .value();
    const auto destination =
        image::PlaneView<std::uint8_t>::create(output, {.width = 256, .height = 2, .stride = 258})
            .value();
    for (std::uint16_t t = 0; t <= 4094; ++t) {
        REQUIRE(methods::apply_otsu(source, destination, {.threshold_bin = t}));
        for (std::size_t p = 0; p < 256; ++p) {
            const auto q = reference_bin(static_cast<std::uint8_t>(p));
            CHECK(output.at(p) == (q <= t ? 0 : 255));
            CHECK(output.at(258 + 255 - p) == (q <= t ? 0 : 255));
        }
        CHECK(output.at(256) == 42);
        CHECK(output.at(257) == 42);
        CHECK(output.at(514) == 42);
        CHECK(output.at(515) == 42);
    }
    core::Budget budget{methods::otsu_scratch_bytes};
    const auto fitted = methods::fit_otsu(source, budget).value();
    std::array<std::uint8_t, 256> ramp{};
    for (std::size_t p = 0; p < ramp.size(); ++p) {
        ramp.at(p) = static_cast<std::uint8_t>(p);
    }
    CHECK(fitted == reference_otsu(ramp));
}
TEST_CASE("Otsu validates and reserves histogram before touching output", "[otsu]") {
    std::array<std::uint8_t, 2> pixels{0, 255};
    std::array<std::uint8_t, 2> output{42, 42};
    const auto source =
        image::PlaneView<std::uint8_t>::create(pixels, {.width = 2, .height = 1, .stride = 2})
            .value();
    const auto destination =
        image::PlaneView<std::uint8_t>::create(output, {.width = 2, .height = 1, .stride = 2})
            .value();
    const auto scheduler = otsu_schedule(4);
    core::Budget refused{methods::otsu_scratch_bytes - 1};
    const auto result =
        methods::otsu(source.as_const(), destination, {.scheduler = scheduler, .budget = refused});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::resource);
    CHECK(refused.used() == 0);
    CHECK(output == std::array<std::uint8_t, 2>{42, 42});
    CHECK(!methods::otsu({}, destination, {.scheduler = scheduler, .budget = refused}));
    CHECK(!methods::otsu(source.as_const(), source, {.scheduler = scheduler, .budget = refused}));
    CHECK(!methods::apply_otsu(source.as_const(), destination, {.threshold_bin = 4095}));
    CHECK(!methods::apply_otsu(source.as_const(), destination,
                               {.threshold_bin = 0, .single_bin_fallback = true}));
    CHECK(output == std::array<std::uint8_t, 2>{42, 42});
    core::Budget exact{methods::otsu_scratch_bytes};
    REQUIRE(
        methods::otsu(source.as_const(), destination, {.scheduler = scheduler, .budget = exact}));
    CHECK(exact.used() == 0);
    CHECK(methods::scratch_bytes(methods::Otsu::create(), 2, 64).value() == 32768);
}
TEST_CASE("Otsu cancellation stops bounded phases and refunds scratch", "[otsu]") {
    std::vector<std::uint8_t> pixels(4097, 255);
    pixels.front() = 0;
    std::vector<std::uint8_t> output(pixels.size(), 42);
    const auto source = image::PlaneView<const std::uint8_t>::create(
                            pixels, {.width = 4097, .height = 1, .stride = 4097})
                            .value();
    const auto destination =
        image::PlaneView<std::uint8_t>::create(output, {.width = 4097, .height = 1, .stride = 4097})
            .value();
    for (const auto checkpoint : {
             core::Checkpoint::allocation,
             core::Checkpoint::initialization,
             core::Checkpoint::measurement,
             core::Checkpoint::solving,
             core::Checkpoint::processing,
         }) {
        for (const auto passes : {0U, 1U}) {
            if (checkpoint == core::Checkpoint::allocation && passes != 0) {
                continue;
            }
            check_otsu_stop(source, destination, checkpoint, passes);
        }
    }
    stop_at = core::Checkpoint::commit;
}
} // namespace docenhance::tests
