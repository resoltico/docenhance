// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "docenhance/io/jpeg.hpp"
#include "docenhance/io/png.hpp"
#include "png_fixture.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
namespace docenhance::tests {
TEST_CASE("PNG source limits are admitted explicitly instead of silently clamped") {
    const auto bytes =
        make_gray_png({.width = 1, .height = 1, .depth = 8, .interlaced = false, .samples = {0}});
    const std::array policies{
        io::PngLimits{.encoded_bytes = 0},
        io::PngLimits{.pixels = 0},
        io::PngLimits{.encoded_bytes = io::png_max_encoded_bytes + 1},
        io::PngLimits{.pixels = io::png_max_pixels + 1},
    };
    for (const auto policy : policies) {
        core::Budget budget{0};
        const auto continuous =
            io::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded, {}, policy);
        REQUIRE(!continuous);
        REQUIRE(continuous.error().code == core::ErrorCode::argument);
        const auto binary = io::decode_grayscale_png(bytes, budget, policy);
        REQUIRE(!binary);
        REQUIRE(binary.error().code == core::ErrorCode::argument);
        REQUIRE(budget.used() == 0);
    }
}
TEST_CASE("Codecs reject unknown profile policies before allocating or decoding") {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): Invalid-policy fixture.
    const auto policy = static_cast<image::ProfilePolicy>(255);
    core::Budget budget{0};
    const auto png = io::decode_png_raster({}, budget, policy);
    REQUIRE(!png);
    REQUIRE(png.error().code == core::ErrorCode::argument);
    const auto jpeg = io::decode_jpeg({}, budget, policy);
    REQUIRE(!jpeg);
    REQUIRE(jpeg.error().code == core::ErrorCode::argument);
    REQUIRE(budget.used() == 0);
}
} // namespace docenhance::tests
