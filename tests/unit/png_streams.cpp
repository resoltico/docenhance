// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/continuous_png.hpp"
#include "docenhance/codec/png_artifact.hpp"
#include "docenhance/codec/png_output.hpp"
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "png_depth_fixture.hpp"
#include "png_fixture.hpp"
#include "png_memory_output.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace docenhance::tests {
namespace {
struct RefusingStream {
    unsigned opens = 0;
    unsigned writes = 0;
    std::stop_source stop;
    bool cancel_on_write = false;
};
bool open_stream(void* const state) {
    ++static_cast<RefusingStream*>(state)->opens;
    return true;
}
bool refuse_write(void* const state, std::span<const std::uint8_t> /*bytes*/) noexcept {
    auto& stream = *static_cast<RefusingStream*>(state);
    ++stream.writes;
    if (stream.cancel_on_write) {
        static_cast<void>(stream.stop.request_stop());
    }
    return false;
}
bool flush_stream(void* /*state*/) noexcept {
    return true;
}
void check_expanded_row(std::span<const std::uint8_t> row, const GrayFixture& fixture, unsigned y) {
    for (unsigned x = 0; x < fixture.width; ++x) {
        CHECK(row.subspan(x, 1).front() == fixture.samples.at((y * fixture.width) + x) * 255);
    }
}
void check_source_expansion(const std::vector<std::uint8_t>& bytes, const GrayFixture& fixture,
                            core::Budget& budget) {
    const auto source = codec::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded);
    REQUIRE(source);
    for (unsigned y = 0; y < fixture.height; ++y) {
        check_expanded_row(source->pixels.view().row(y), fixture, y);
    }
}
void check_png1_bytes(std::span<const std::byte> bytes, const codec::PngImage& intended,
                      core::Budget& budget) {
    CHECK(bytes.subspan(24, 1).front() == std::byte{1});
    REQUIRE(codec::compare_png(bytes, intended, budget));
}
void check_png1_white_count(const core::Result<codec::PngArtifact>& observed, unsigned width) {
    REQUIRE(observed);
    CHECK(observed->white_samples == width);
}
void check_png1_tail_refusal(const core::Result<codec::PngArtifact>& observed,
                             const std::vector<std::uint8_t>& tails,
                             const codec::PngImage& intended, core::Budget& budget) {
    REQUIRE(!observed);
    CHECK(observed.error().code == core::ErrorCode::input);
    const auto compared = codec::compare_png(std::as_bytes(std::span{tails}), intended, budget);
    REQUIRE(!compared);
    CHECK(compared.error().code == core::ErrorCode::output_verify);
}
void check_png1_refund_and_padding(const core::Budget& budget,
                                   const std::vector<std::uint8_t>& padded, unsigned width) {
    CHECK(budget.used() == 0);
    CHECK(padded.at(width) == 0xa5);
    CHECK(padded.back() == 0xa5);
}
void check_png1_pair(unsigned width, unsigned polarity) {
    GrayFixture fixture{
        .width = width,
        .height = 2,
        .depth = 1,
        .interlaced = false,
        .samples = std::vector<std::uint8_t>(),
    };
    for (unsigned i = 0; i < width * 2; ++i) {
        fixture.samples.push_back(static_cast<std::uint8_t>((i + polarity) % 2));
    }
    const auto stride = width + 3;
    std::vector<std::uint8_t> padded(std::size_t{stride} * 2, 0xa5);
    for (unsigned y = 0; y < 2; ++y) {
        std::ranges::copy(std::span{fixture.samples}.subspan(std::size_t{y} * width, width),
                          std::span{padded}.subspan(std::size_t{y} * stride, width).begin());
    }
    const auto pixels = image::PlaneView<const std::uint8_t>::create(
        padded, {.width = width, .height = 2, .stride = stride});
    REQUIRE(pixels);
    const codec::PngImage intended{
        .shape =
            {
                .width = width,
                .height = 2,
                .depth = docenhance::tests::fixture_png_depth(1),
            },
        .pixels = *pixels,
        .profile = std::span<const std::byte>(),
        .resolution = std::nullopt,
    };
    core::Budget budget{std::size_t{2} * 1024 * 1024};
    {
        const auto encoded = codec::encode_png(intended, budget);
        REQUIRE(encoded);
        check_png1_bytes(encoded->bytes(), intended, budget);
        const auto counted = codec::observe_png_artifact(encoded->bytes(), budget, {});
        check_png1_white_count(counted, width);
        const auto independent = make_gray_png(fixture);
        REQUIRE(codec::compare_png(std::as_bytes(std::span{independent}), intended, budget));
        fixture.unused_tail = 0xff;
        const auto tails = make_gray_png(fixture);
        const auto observed =
            codec::observe_png_artifact(std::as_bytes(std::span{tails}), budget, {});
        if (width % 8 == 0) {
            check_png1_white_count(observed, width);
        } else {
            check_png1_tail_refusal(observed, tails, intended, budget);
        }
        check_source_expansion(tails, fixture, budget);
    }
    check_png1_refund_and_padding(budget, padded, width);
}
} // namespace
TEST_CASE("Materialized PNG byte output rejects absent callbacks and empty input", "[codec]") {
    core::Budget budget{std::size_t{1024} * 1024};
    auto image = image::Plane<std::uint8_t>::allocate(budget, 8, 8);
    REQUIRE(image);
    std::ranges::fill(image->view().storage(), 0);
    const codec::PngImage intended{
        .shape = {.width = 8, .height = 8, .depth = image::PngDepth()},
        .pixels = image->view().as_const(),
        .profile = std::span<const std::byte>(),
        .resolution = std::nullopt,
    };
    const auto charged = budget.used();
    const codec::PngOutput absent_output;
    const auto output = codec::write_png(absent_output, intended, budget, {});
    REQUIRE(!output);
    CHECK(output.error().code == core::ErrorCode::argument);
    const auto input = codec::compare_png({}, intended, budget);
    REQUIRE(!input);
    CHECK(input.error().code == core::ErrorCode::output_verify);
    CHECK(budget.used() == charged);
}
TEST_CASE("PNG byte-write errors precede a newly requested cancellation", "[codec]") {
    for (const bool cancel : {false, true}) {
        core::Budget budget{std::size_t{1024} * 1024};
        auto image = image::Plane<std::uint8_t>::allocate(budget, 8, 8);
        REQUIRE(image);
        const auto view = image->view();
        for (std::uint32_t y = 0; y < image->height(); ++y) {
            const auto row = view.row(y);
            for (auto& pixel : row) {
                pixel = 0;
            }
        }
        const auto charged = budget.used();
        RefusingStream stream;
        stream.cancel_on_write = cancel;
        const core::Cancellation control{stream.stop.get_token()};
        const codec::PngOutput output{
            .state = &stream,
            .open = open_stream,
            .write = refuse_write,
            .flush = flush_stream,
        };
        const auto result =
            codec::write_png(output,
                             {
                                 .shape = {.width = 8, .height = 8, .depth = image::PngDepth()},
                                 .pixels = image->view().as_const(),
                                 .profile = std::span<const std::byte>(),
                                 .resolution = std::nullopt,
                             },
                             budget, control);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::output);
        CHECK(stream.opens == 1);
        CHECK(stream.writes > 0);
        CHECK(budget.used() == charged);
    }
}
TEST_CASE("Already cancelled PNG byte encoding never opens the stream", "[codec]") {
    core::Budget budget{std::size_t{1024} * 1024};
    auto image = image::Plane<std::uint8_t>::allocate(budget, 8, 8);
    REQUIRE(image);
    RefusingStream stream;
    static_cast<void>(stream.stop.request_stop());
    const core::Cancellation control{stream.stop.get_token()};
    const codec::PngOutput output{
        .state = &stream,
        .open = open_stream,
        .write = refuse_write,
        .flush = flush_stream,
    };
    const auto result =
        codec::write_png(output,
                         {
                             .shape = {.width = 8, .height = 8, .depth = image::PngDepth()},
                             .pixels = image->view().as_const(),
                             .profile = std::span<const std::byte>(),
                             .resolution = std::nullopt,
                         },
                         budget, control);
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::cancelled);
    CHECK(stream.opens == 0);
    CHECK(stream.writes == 0);
}
TEST_CASE("Encoded PNG capacity stays charged and move consumes the emitted-byte borrow",
          "[codec][resource]") {
    core::Budget budget{24576};
    {
        codec::PngMemoryOutput output{budget};
        std::array<std::uint8_t, 8192> first{};
        std::array<std::uint8_t, 1> last{37};
        REQUIRE(output.append(first));
        CHECK(budget.used() == 8192);
        REQUIRE(output.append(last));
        CHECK(budget.used() == 16384);
        CHECK(budget.peak() == 24576);
        auto encoded = std::move(output).finish();
        REQUIRE(encoded);
        CHECK(encoded->bytes().size() == 8193);
        CHECK(encoded->bytes().back() == std::byte{37});
        const auto moved = std::move(*encoded);
        CHECK(!encoded->ready());
        CHECK(encoded->bytes().empty());
        CHECK(moved.ready());
        CHECK(moved.bytes().size() == 8193);
        CHECK(budget.used() == 16384);
    }
    CHECK(budget.used() == 0);
    core::Budget short_budget{16384};
    {
        codec::PngMemoryOutput output{short_budget};
        std::array<std::uint8_t, 8192> first{};
        REQUIRE(output.append(first));
        CHECK(!output.append(std::span{first}.first(1)));
        CHECK(output.exhausted());
        CHECK(short_budget.used() == 8192);
    }
    CHECK(short_budget.used() == 0);
}

TEST_CASE("Canonical PNG1 reads physical tails before combine and preserves source expansion",
          "[codec][png1]") {
    for (const auto width : {1U, 7U, 8U, 9U, 15U, 16U, 17U}) {
        for (const auto polarity : {0U, 1U}) {
            check_png1_pair(width, polarity);
        }
    }
}
} // namespace docenhance::tests
