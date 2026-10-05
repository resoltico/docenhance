// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

namespace docenhance::tests {
TEST_CASE("Decoded precision admits only complete byte or word samples", "[values]") {
    static_assert(!std::is_constructible_v<image::SampleDepth, unsigned>);
    for (const auto bits :
         {0U, 1U, 2U, 4U, 7U, 9U, 12U, 15U, 17U, 32U, std::numeric_limits<unsigned>::max()}) {
        CHECK(!image::SampleDepth::from_bits(bits));
    }
    REQUIRE(image::SampleDepth::from_bits(8));
    REQUIRE(image::SampleDepth::from_bits(16));
    CHECK(image::SampleDepth::byte().bits() == 8);
    CHECK(image::SampleDepth::word().bytes() == 2);
    const std::array<std::uint8_t, 2> bytes{0x12, 0x34};
    CHECK(image::read_sample(bytes, image::SampleDepth::byte()) == 0x12);
    CHECK(image::read_sample(bytes, image::SampleDepth::word()) == 0x1234);
}
TEST_CASE("Admitted EXIF orientations preserve exact non-square corner mappings", "[values]") {
    static_assert(!std::is_constructible_v<image::Orientation, unsigned>);
    CHECK(!image::Orientation::from_code(0));
    CHECK(!image::Orientation::from_code(9));
    CHECK(!image::Orientation::from_code(std::numeric_limits<unsigned>::max()));
    const image::RasterShape source{.width = 3, .height = 2};
    constexpr std::array first{
        image::Coordinate{.x = 0, .y = 0}, image::Coordinate{.x = 2, .y = 0},
        image::Coordinate{.x = 2, .y = 1}, image::Coordinate{.x = 0, .y = 1},
        image::Coordinate{.x = 0, .y = 0}, image::Coordinate{.x = 0, .y = 1},
        image::Coordinate{.x = 2, .y = 1}, image::Coordinate{.x = 2, .y = 0},
    };
    constexpr std::array last{
        image::Coordinate{.x = 2, .y = 1}, image::Coordinate{.x = 0, .y = 1},
        image::Coordinate{.x = 0, .y = 0}, image::Coordinate{.x = 2, .y = 0},
        image::Coordinate{.x = 2, .y = 1}, image::Coordinate{.x = 2, .y = 0},
        image::Coordinate{.x = 0, .y = 0}, image::Coordinate{.x = 0, .y = 1},
    };
    for (unsigned code = 1; code <= 8; ++code) {
        const auto admitted = image::Orientation::from_code(code);
        if (!admitted) {
            FAIL("all eight orientation codes must be admitted");
            return;
        }
        const auto orientation = *admitted;
        const auto shape = image::oriented_shape(source, orientation);
        CHECK(shape.width == (code < 5 ? 3U : 2U));
        CHECK(shape.height == (code < 5 ? 2U : 3U));
        const auto a = image::source_coordinate(source, orientation, {.x = 0, .y = 0});
        const auto b = image::source_coordinate(source, orientation,
                                                {.x = shape.width - 1, .y = shape.height - 1});
        CHECK(a.x == first.at(code - 1).x);
        CHECK(a.y == first.at(code - 1).y);
        CHECK(b.x == last.at(code - 1).x);
        CHECK(b.y == last.at(code - 1).y);
    }
}
TEST_CASE("An unimplemented capability can never claim staging or publication", "[values]") {
    core::Error error{.code = core::ErrorCode::not_implemented, .message = "not implemented"};
    CHECK(std::string(error.identifier()) == "E_NOT_IMPLEMENTED");
    CHECK(error.exit_code() == core::ExitCode::processing);
    CHECK(error.valid_publication());
    for (const auto state : {
             core::Publication::not_published,
             core::Publication::completed,
             core::Publication::unknown,
         }) {
        error.publication = state;
        CHECK(!error.valid_publication());
    }
}
} // namespace docenhance::tests
