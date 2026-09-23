// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "png_fixture.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace docenhance::tests {
namespace {
constexpr std::size_t codec_budget = std::size_t{8} * 1024 * 1024;
GrayFixture sample_fixture(unsigned depth, bool interlaced, std::uint32_t width) {
    GrayFixture fixture{
        .width = width,
        .height = 9,
        .depth = depth,
        .interlaced = interlaced,
        .samples = {},
    };
    for (std::uint32_t i = 0; i < fixture.width * fixture.height; ++i) {
        fixture.samples.push_back(static_cast<std::uint8_t>(i % (1U << depth)));
    }
    return fixture;
}
void verify_samples(image::PlaneView<const std::uint8_t> decoded, const GrayFixture& fixture) {
    CHECK(decoded.width() == fixture.width);
    CHECK(decoded.height() == fixture.height);
    const unsigned maximum = (1U << fixture.depth) - 1U;
    std::vector<std::uint8_t> expected;
    expected.reserve(fixture.samples.size());
    for (const auto sample : fixture.samples) {
        expected.push_back(static_cast<std::uint8_t>(sample * 255U / maximum));
    }
    for (std::uint32_t y = 0; y < decoded.height(); ++y) {
        const auto row = std::span{expected}.subspan(std::size_t{y} * fixture.width, fixture.width);
        CHECK(std::ranges::equal(decoded.row(y), row));
    }
}
void check_samples(const GrayFixture& fixture) {
    const auto bytes = make_gray_png(fixture);
    core::Budget budget{codec_budget};
    {
        const auto decoded = io::decode_grayscale_png(bytes, budget);
        REQUIRE(decoded);
        verify_samples(decoded->view(), fixture);
    }
    CHECK(budget.used() == 0);
}
void check_filters(GrayFixture fixture) {
    for (const unsigned filter : {0U, 1U, 2U, 3U, 4U}) {
        fixture.filter = filter;
        check_samples(fixture);
    }
}
} // namespace
TEST_CASE("Independent PNG fixtures preserve every grayscale sample and Adam7 pass", "[png]") {
    for (const unsigned depth : {1U, 2U, 4U, 8U}) {
        for (const bool interlaced : {false, true}) {
            for (const std::uint32_t width : {1U, 2U, 9U, 32U}) {
                check_filters(sample_fixture(depth, interlaced, width));
            }
        }
    }
    // PNG specification's fixed IEND checksum anchors the independent CRC implementation.
    constexpr auto iend = std::to_array<std::uint8_t>({'I', 'E', 'N', 'D'});
    CHECK(png_crc(iend) == 0xae426082U);
}
TEST_CASE("Every truncated PNG prefix is rejected and refunds its decoder budget", "[png]") {
    const auto bytes = make_gray_png(sample_fixture(4, true, 9));
    core::Budget budget{codec_budget};
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        const auto decoded = io::decode_grayscale_png(std::span{bytes}.first(size), budget);
        REQUIRE(!decoded);
        CHECK(decoded.error().code == core::ErrorCode::input);
        CHECK(budget.used() == 0);
    }
}
TEST_CASE("PNG limits can only tighten and codec allocation refusal refunds every block", "[png]") {
    const auto bytes = make_gray_png(sample_fixture(8, false, 9));
    core::Budget budget{codec_budget};
    CHECK(
        io::decode_grayscale_png(bytes, budget, {.encoded_bytes = bytes.size() - 1}).error().code ==
        core::ErrorCode::resource);
    CHECK(io::decode_grayscale_png(bytes, budget, {.pixels = 80}).error().code ==
          core::ErrorCode::resource);
    CHECK(io::decode_grayscale_png(bytes, budget, {.pixels = 0}).error().code ==
          core::ErrorCode::argument);
    CHECK(io::decode_grayscale_png(bytes, budget, {.pixels = 40'000'001}).error().code ==
          core::ErrorCode::argument);
    CHECK(io::decode_grayscale_png(bytes, budget, {.encoded_bytes = 0}).error().code ==
          core::ErrorCode::argument);
    CHECK(budget.used() == 0);
    for (const std::size_t limit : {0U, 64U, 1024U, 4096U, 16384U, 32768U}) {
        core::Budget constrained{limit};
        {
            const auto decoded = io::decode_grayscale_png(bytes, constrained);
            REQUIRE(!decoded);
            CHECK(decoded.error().code == core::ErrorCode::resource);
        }
        CHECK(constrained.used() == 0);
    }
}
TEST_CASE("PNG CRC checking is identical on the memory path", "[png]") {
    auto bytes = make_gray_png(sample_fixture(8, false, 2));
    bytes.at(29) ^= 1U;
    core::Budget budget{codec_budget};
    const auto decoded = io::decode_grayscale_png(bytes, budget);
    REQUIRE(!decoded);
    CHECK(decoded.error().code == core::ErrorCode::input);
    CHECK(budget.used() == 0);
}
} // namespace docenhance::tests
