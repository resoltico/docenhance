// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/continuous_png.hpp"
#include "docenhance/codec/jpeg.hpp"
#include "docenhance/codec/png.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "png_fixture.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
namespace docenhance::tests {
TEST_CASE("PNG source limits are admitted explicitly instead of silently clamped") {
    const auto bytes =
        make_gray_png({.width = 1, .height = 1, .depth = 8, .interlaced = false, .samples = {0}});
    const std::array policies{
        codec::PngLimits{.encoded_bytes = 0},
        codec::PngLimits{.pixels = 0},
        codec::PngLimits{.encoded_bytes = codec::png_max_encoded_bytes + 1},
        codec::PngLimits{.pixels = codec::png_max_pixels + 1},
    };
    for (const auto policy : policies) {
        core::Budget budget{0};
        const auto raster =
            codec::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded, {}, policy);
        REQUIRE(!raster);
        REQUIRE(raster.error().code == core::ErrorCode::argument);
        REQUIRE(budget.used() == 0);
    }
}
TEST_CASE("Codecs reject unknown profile policies before allocating or decoding") {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): Invalid-policy fixture.
    const auto policy = static_cast<image::ProfilePolicy>(255);
    core::Budget budget{0};
    const auto png = codec::decode_png_raster({}, budget, policy);
    REQUIRE(!png);
    REQUIRE(png.error().code == core::ErrorCode::argument);
    const auto jpeg = codec::decode_jpeg({}, budget, policy);
    REQUIRE(!jpeg);
    REQUIRE(jpeg.error().code == core::ErrorCode::argument);
    REQUIRE(budget.used() == 0);
}
} // namespace docenhance::tests
