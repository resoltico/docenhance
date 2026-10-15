// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stop_token>

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
} // namespace
TEST_CASE("PNG byte streams reject absent callbacks before use", "[codec]") {
    core::Budget budget{std::size_t{1024} * 1024};
    auto image = image::Plane<std::uint8_t>::allocate(budget, 8, 8).value();
    image::PlaneRows rows{image.view().as_const()};
    const auto charged = budget.used();
    const codec::PngOutput absent_output;
    const codec::PngReadStream absent_input;
    const core::Cancellation control;
    const auto output = codec::encode_png(absent_output, image.view().as_const(), budget);
    REQUIRE(!output);
    CHECK(output.error().code == core::ErrorCode::argument);
    const auto input = codec::verify_png_rows(absent_input, rows, budget, control);
    REQUIRE(!input);
    CHECK(input.error().code == core::ErrorCode::argument);
    CHECK(budget.used() == charged);
}
TEST_CASE("PNG byte-write errors precede a newly requested cancellation", "[codec]") {
    for (const bool cancel : {false, true}) {
        core::Budget budget{std::size_t{1024} * 1024};
        auto image = image::Plane<std::uint8_t>::allocate(budget, 8, 8).value();
        const auto view = image.view();
        for (std::uint32_t y = 0; y < image.height(); ++y) {
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
        const auto result = codec::encode_png(output, image.view().as_const(), budget, control);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::output);
        CHECK(stream.opens == 1);
        CHECK(stream.writes > 0);
        CHECK(budget.used() == charged);
    }
}
TEST_CASE("Already cancelled PNG byte encoding never opens the stream", "[codec]") {
    core::Budget budget{std::size_t{1024} * 1024};
    auto image = image::Plane<std::uint8_t>::allocate(budget, 8, 8).value();
    RefusingStream stream;
    static_cast<void>(stream.stop.request_stop());
    const core::Cancellation control{stream.stop.get_token()};
    const codec::PngOutput output{
        .state = &stream,
        .open = open_stream,
        .write = refuse_write,
        .flush = flush_stream,
    };
    const auto result = codec::encode_png(output, image.view().as_const(), budget, control);
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::cancelled);
    CHECK(stream.opens == 0);
    CHECK(stream.writes == 0);
}
} // namespace docenhance::tests
