// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/codec/continuous_png.hpp"
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "png_context.hpp"
#include "png_fixture.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <stop_token>
#include <vector>
namespace docenhance::tests {
namespace {
constexpr std::size_t codec_budget = std::size_t{8} * 1024 * 1024;
bool native_png_refusal(const codec::PngContext& context, codec::PngInput* const input) {
    // Every resource owner is in the test's caller, outside this jump frame.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(std::begin(context.memory.jump)) != 0) {
        return true;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    if (input != nullptr) {
        codec::install_png_reader(context, *input);
        png_read_info(context.png, context.info);
        return false;
    }
    png_error(context.png, "controlled native PNG refusal");
}
struct NativePngInput {
    std::span<const std::uint8_t> bytes;
    std::stop_source stop;
    bool refuse = false;
    unsigned reads = 0;
};
bool native_read(void* const state, std::span<std::uint8_t> output) noexcept {
    auto& input = *static_cast<NativePngInput*>(state);
    ++input.reads;
    if (input.refuse) {
        static_cast<void>(input.stop.request_stop());
        return false;
    }
    if (output.size() > input.bytes.size()) {
        return false;
    }
    std::ranges::copy(input.bytes.first(output.size()), output.begin());
    input.bytes = input.bytes.subspan(output.size());
    return true;
}
void check_refusal(const core::Result<image::Raster>& decoded, core::ErrorCode error) {
    REQUIRE(!decoded);
    CHECK(decoded.error().code == error);
}
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
void verify_gray_rows(image::PlaneView<const std::uint8_t> decoded, const GrayFixture& fixture,
                      const std::vector<std::uint8_t>& expected) {
    for (std::uint32_t y = 0; y < decoded.height(); ++y) {
        const auto row = std::span{expected}.subspan(std::size_t{y} * fixture.width, fixture.width);
        CHECK(std::ranges::equal(decoded.row(y), row));
    }
}
void verify_samples(const image::Raster& raster, const GrayFixture& fixture) {
    CHECK(raster.shape.width == fixture.width);
    CHECK(raster.shape.height == fixture.height);
    CHECK(raster.shape.model == image::SampleModel::gray);
    CHECK(raster.shape.depth == image::SampleDepth::byte());
    const auto decoded = raster.pixels.view();
    CHECK(decoded.width() == fixture.width);
    CHECK(decoded.height() == fixture.height);
    const unsigned maximum = (1U << fixture.depth) - 1U;
    std::vector<std::uint8_t> expected;
    expected.reserve(fixture.samples.size());
    for (const auto sample : fixture.samples) {
        expected.push_back(static_cast<std::uint8_t>(sample * 255U / maximum));
    }
    verify_gray_rows(decoded, fixture, expected);
}
void check_samples(const GrayFixture& fixture) {
    const auto bytes = make_gray_png(fixture);
    core::Budget budget{codec_budget};
    {
        const auto decoded =
            codec::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded);
        REQUIRE(decoded);
        verify_samples(*decoded, fixture);
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
TEST_CASE("A protected PNG error returns to its native frame and refunds codec ownership",
          "[png][ownership]") {
    core::Budget budget{codec_budget};
    {
        const codec::PngContext context{budget, false};
        REQUIRE(context.png != nullptr);
        REQUIRE(context.info != nullptr);
        REQUIRE(budget.used() > 0);
        REQUIRE(native_png_refusal(context, nullptr));
        const auto error = context.error(core::ErrorCode::input);
        CHECK(error.code == core::ErrorCode::input);
        CHECK(error.message == "controlled native PNG refusal");
    }
    CHECK(budget.used() == 0);
}
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
        const auto decoded = codec::decode_png_raster(std::span{bytes}.first(size), budget,
                                                      image::ProfilePolicy::embedded);
        REQUIRE(!decoded);
        CHECK(decoded.error().code == core::ErrorCode::input);
        CHECK(budget.used() == 0);
    }
}
TEST_CASE("PNG limits can only tighten and codec allocation refusal refunds every block", "[png]") {
    const auto bytes = make_gray_png(sample_fixture(8, false, 9));
    core::Budget budget{codec_budget};
    check_refusal(codec::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded, {},
                                           {.encoded_bytes = bytes.size() - 1}),
                  core::ErrorCode::resource);
    check_refusal(
        codec::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded, {}, {.pixels = 80}),
        core::ErrorCode::resource);
    check_refusal(
        codec::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded, {}, {.pixels = 0}),
        core::ErrorCode::argument);
    check_refusal(codec::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded, {},
                                           {.pixels = 40'000'001}),
                  core::ErrorCode::argument);
    check_refusal(codec::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded, {},
                                           {.encoded_bytes = 0}),
                  core::ErrorCode::argument);
    CHECK(budget.used() == 0);
    for (const std::size_t limit : {0U, 64U, 1024U, 4096U, 16384U, 32768U}) {
        core::Budget constrained{limit};
        {
            const auto decoded =
                codec::decode_png_raster(bytes, constrained, image::ProfilePolicy::embedded);
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
    const auto decoded = codec::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded);
    REQUIRE(!decoded);
    CHECK(decoded.error().code == core::ErrorCode::input);
    CHECK(budget.used() == 0);
}
TEST_CASE("Actual native PNG byte reads preserve failure priority and caller ownership", "[png]") {
    const auto bytes = make_gray_png(sample_fixture(8, false, 2));
    for (const bool refuse : {false, true}) {
        NativePngInput source{.bytes = bytes, .stop = {}, .refuse = refuse};
        core::Budget budget{codec_budget};
        {
            const codec::PngContext context{budget, false,
                                            core::Cancellation{source.stop.get_token()}};
            REQUIRE(context.ready());
            codec::PngInput input{
                .state = &source,
                .read = native_read,
                .remaining = refuse ? std::size_t{8} : bytes.size(),
            };
            CHECK(native_png_refusal(context, &input) == refuse);
            CHECK(source.reads > 0);
            if (refuse) {
                CHECK(context.error(core::ErrorCode::input).code == core::ErrorCode::input);
                CHECK(source.stop.stop_requested());
            } else {
                CHECK(!source.stop.stop_requested());
                CHECK(png_get_image_width(context.png, context.info) == 2);
                CHECK(png_get_image_height(context.png, context.info) == 9);
            }
        }
        CHECK(budget.used() == 0);
    }
}
TEST_CASE("Malformed PNG admission before a pending stop retains its real error", "[png]") {
    std::stop_source source;
    static_cast<void>(request_stop(source));
    const core::Cancellation control{source.get_token()};
    core::Budget budget{codec_budget};
    const std::array<std::uint8_t, 8> bad_signature{};
    check_refusal(
        codec::decode_png_raster(bad_signature, budget, image::ProfilePolicy::embedded, control),
        core::ErrorCode::input);
    const auto bytes = make_gray_png(sample_fixture(8, false, 2));
    check_refusal(codec::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded, control),
                  core::ErrorCode::cancelled);
    CHECK(budget.used() == 0);
}
} // namespace docenhance::tests
