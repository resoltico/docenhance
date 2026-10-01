// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/jpeg.hpp"
#include "jpeg_fixture.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
namespace docenhance::tests {
namespace {
double chroma_value(unsigned component, unsigned x, unsigned y) {
    if (component == 1) {
        return 96.0 + static_cast<double>(((x * 40) + (y * 10)) % 100);
    }
    return 180.0 - static_cast<double>(((x * 30) + (y * 20)) % 100);
}
double sample(unsigned component, image::Coordinate pixel) {
    constexpr unsigned factor = 2;
    const auto chroma_width = (33U + factor - 1) / factor;
    const auto chroma_height = (33U + factor - 1) / factor;
    const auto sx = std::clamp((static_cast<double>(pixel.x) - 0.5) / factor, 0.0,
                               static_cast<double>(chroma_width - 1));
    const auto sy = std::clamp((static_cast<double>(pixel.y) - 0.5) / factor, 0.0,
                               static_cast<double>(chroma_height - 1));
    const auto left = static_cast<unsigned>(std::floor(sx));
    const auto above = static_cast<unsigned>(std::floor(sy));
    const auto right = std::min(left + 1, chroma_width - 1);
    const auto below = std::min(above + 1, chroma_height - 1);
    const auto fx = sx - left;
    const auto fy = sy - above;
    const auto top = std::lerp(chroma_value(component, left / 8, above / 8),
                               chroma_value(component, right / 8, above / 8), fx);
    const auto bottom = std::lerp(chroma_value(component, left / 8, below / 8),
                                  chroma_value(component, right / 8, below / 8), fx);
    return std::lerp(top, bottom, fy);
}
std::array<int, 3> rgb_reference(unsigned x, unsigned y) {
    const auto cb = sample(1, {.x = x, .y = y}) - 128.0;
    const auto cr = sample(2, {.x = x, .y = y}) - 128.0;
    const std::array<int, 3> values{
        static_cast<int>(std::floor(160.0 + (1.402 * cr) + 0.5)),
        static_cast<int>(std::floor(160.0 - (0.344136 * cb) - (0.714136 * cr) + 0.5)),
        static_cast<int>(std::floor(160.0 + (1.772 * cb) + 0.5)),
    };
    auto result = values;
    for (auto& value : result) {
        value = std::clamp(value, 0, 255);
    }
    return result;
}
void check_pixel(std::span<const std::uint8_t> pixel, const std::array<int, 3>& expected) {
    for (std::size_t channel = 0; channel < expected.size(); ++channel) {
        const auto difference = static_cast<int>(pixel[channel]) - expected.at(channel);
        CHECK(std::abs(difference) <= 2);
    }
}
void check_row(std::span<const std::uint8_t> row, unsigned y) {
    for (unsigned x = 0; x < 33; ++x) {
        check_pixel(row.subspan(std::size_t{x} * 3, 3), rgb_reference(x, y));
    }
}
} // namespace
TEST_CASE("JPEG fancy chroma upsampling agrees with an independent centered reference", "[jpeg]") {
    for (const auto* const process : {"baseline", "progressive"}) {
        core::Budget budget{std::size_t{8} * 1024 * 1024};
        auto decoded =
            io::decode_jpeg(jpeg_fixture(std::string("chroma-patch-") + process + ".jpeg"), budget,
                            image::ProfilePolicy::embedded);
        REQUIRE(decoded);
        for (unsigned y = 0; y < 33; ++y) {
            check_row(decoded->raster.pixels.view().row(y), y);
        }
    }
}
} // namespace docenhance::tests
