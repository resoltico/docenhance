// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>

namespace docenhance::tests {
TEST_CASE("JPEG facts enforce shared bounds without entering a decoder", "[values][jpeg]") {
    constexpr image::Sampling unit_sampling{.horizontal = 1, .vertical = 1};
    const image::JpegFacts source{
        .width = 65500,
        .height = 1,
        .color = image::JpegColor::ycbcr,
        .process = image::JpegProcess::progressive,
        .scans = 128,
        .component_ids = {1, 2, 3},
        .sampling =
            {
                image::Sampling{.horizontal = 4, .vertical = 2},
                unit_sampling,
                unit_sampling,
            },
        .adobe_transform = std::nullopt,
        .jfif_resolution = std::nullopt,
        .exif_resolution = std::nullopt,
    };
    REQUIRE(image::valid_source_facts(source));
    auto altered = source;
    altered.width = 65501; // Still below the pixel ceiling; only the per-axis domain is exceeded.
    CHECK(!image::valid_source_facts(altered));
    altered = source;
    altered.width = 1;
    altered.height = 65500;
    REQUIRE(image::valid_source_facts(altered));
    altered.height = 65501;
    CHECK(!image::valid_source_facts(altered));
    for (const auto scans : {0U, 129U}) {
        altered = source;
        altered.scans = scans;
        CHECK(!image::valid_source_facts(altered));
    }
    altered = source;
    altered.sampling.front() = {.horizontal = 2, .vertical = 4};
    REQUIRE(image::valid_source_facts(altered));
    for (const auto sample : std::array{
             image::Sampling{.horizontal = 5, .vertical = 1},
             image::Sampling{.horizontal = 1, .vertical = 5},
             image::Sampling{.horizontal = 3, .vertical = 3},
         }) {
        altered = source;
        altered.sampling.front() = sample;
        // 5x1/1x5 isolate sampling admission; 3x3 is legal per-axis but exceeds ten MCU blocks.
        CHECK(!image::valid_source_facts(altered));
    }
}
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
