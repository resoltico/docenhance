// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/record/source_fields.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/schema.hpp"
#include "docenhance/record/source.hpp"

#include <catch2/catch_test_macros.hpp>
#include <optional>

namespace docenhance::tests {
TEST_CASE("JPEG container vectors carry exactly actual components with bounded admission") {
    image::JpegFacts source{
        .width = 2,
        .height = 1,
        .scans = 1,
        .component_ids = {1, 0, 0},
        .adobe_transform = std::nullopt,
        .jfif_resolution = std::nullopt,
        .exif_resolution = std::nullopt,
    };
    REQUIRE(image::valid_source_facts(source));
    const auto container = record::source_container(source);
    REQUIRE(container);
    const auto encoding = record::Codec::encode(*container);
    REQUIRE(encoding);
    const auto& encoded = *encoding;
    CHECK(encoded["component_ids"] == record::Json::array({1}));
    CHECK(encoded["sampling"].size() == 1);
    const auto restored = record::Codec::decode<record::SourceContainer>(encoded);
    REQUIRE(restored);
    REQUIRE(record::decoded_source(*restored));
    const auto reencoded = record::Codec::encode(*restored);
    REQUIRE(reencoded);
    CHECK(*reencoded == encoded);
    for (const auto& invalid : record::Json::array({
             record::Json::array(),
             record::Json::array({1, 2}),
             record::Json::array({1, 2, 3, 4}),
             record::Json::array({256}),
             record::Json::array({true}),
             record::Json::array({1.5}),
         })) {
        auto changed = encoded;
        changed["component_ids"] = invalid;
        CHECK(!record::Codec::decode<record::SourceContainer>(changed));
    }
    auto changed = encoded;
    changed["components"] = 3;
    CHECK(!record::Codec::decode<record::SourceContainer>(changed));
    changed = encoded;
    changed["sampling"][0]["horizontal"] = 2;
    CHECK(!record::Codec::decode<record::SourceContainer>(changed));
    source.component_ids.at(0) = 256;
    const auto refused = record::source_container(source);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::invariant);
    const auto description = record::Schema::of<record::JpegContainer>();
    REQUIRE(description);
    const auto& schema = (*description)["properties"];
    CHECK(schema["component_ids"]["maxItems"] == image::jpeg_components);
    CHECK(schema["component_ids"]["items"]["maximum"] == 255);
    CHECK(schema["sampling"]["maxItems"] == image::jpeg_components);
}
TEST_CASE("JPEG actual color and metadata predicates remain shared with source admission") {
    image::JpegFacts source{
        .width = 2,
        .height = 1,
        .color = image::JpegColor::rgb,
        .scans = 1,
        .component_ids = {'R', 'G', 'B'},
        .adobe_transform = std::nullopt,
        .jfif_resolution = std::nullopt,
        .exif_resolution = std::nullopt,
    };
    const auto container = record::source_container(source);
    REQUIRE(container);
    const auto encoding = record::Codec::encode(*container);
    REQUIRE(encoding);
    const auto& encoded = *encoding;
    REQUIRE(record::Codec::decode<record::SourceContainer>(encoded));
    for (const auto& field : {"decoder_policy", "coding", "resolution_precedence"}) {
        auto changed = encoded;
        changed[field] = "unknown";
        CHECK(!record::Codec::decode<record::SourceContainer>(changed));
    }
    auto changed = encoded;
    changed["component_ids"][1] = 'R';
    CHECK(!record::Codec::decode<record::SourceContainer>(changed));
    changed = encoded;
    changed["jfif_present"] = true;
    CHECK(!record::Codec::decode<record::SourceContainer>(changed));
    changed = encoded;
    changed["resolution_conflict"] = true;
    CHECK(!record::Codec::decode<record::SourceContainer>(changed));
}
TEST_CASE("TIFF contextual source predicates survive typed record round trips") {
    const image::TiffFacts source{
        .width = 2,
        .height = 1,
        .orientation = image::Orientation(),
        .resolution = std::nullopt,
    };
    const auto container = record::source_container(source);
    REQUIRE(container);
    const auto encoding = record::Codec::encode(*container);
    REQUIRE(encoding);
    const auto& encoded = *encoding;
    const auto restored = record::Codec::decode<record::SourceContainer>(encoded);
    REQUIRE(restored);
    CHECK(record::decoded_source(*restored));
    const auto reencoded = record::Codec::encode(*restored);
    REQUIRE(reencoded);
    CHECK(*reencoded == encoded);
    const record::Json replacements{
        {"pages", 2},       {"precision", 2},   {"photometric", 4},
        {"compression", 2}, {"orientation", 0}, {"samples", 2},
        {"predictor", 2},   {"planar", 0},      {"alpha", 3},
    };
    for (const auto& [key, value] : replacements.items()) {
        auto changed = encoded;
        changed[key] = value;
        CHECK(!record::Codec::decode<record::SourceContainer>(changed));
    }
    auto fax = encoded;
    fax["precision"] = 1;
    fax["compression"] = 4;
    CHECK(record::Codec::decode<record::SourceContainer>(fax));
    fax["photometric"] = 2;
    fax["samples"] = 3;
    CHECK(!record::Codec::decode<record::SourceContainer>(fax));
    auto palette = encoded;
    palette["photometric"] = 3;
    CHECK(record::Codec::decode<record::SourceContainer>(palette));
    palette["precision"] = 16;
    CHECK(!record::Codec::decode<record::SourceContainer>(palette));
}
TEST_CASE("Source factories refuse invalid in-memory PNG and TIFF facts without conversion") {
    image::PngFacts png{.width = 2, .height = 1};
    REQUIRE(record::source_container(png));
    png.depth = 3;
    const auto invalid_png = record::source_container(png);
    REQUIRE(!invalid_png);
    CHECK(invalid_png.error().code == core::ErrorCode::invariant);
    image::TiffFacts tiff{
        .width = 2,
        .height = 1,
        .orientation = image::Orientation(),
        .resolution = std::nullopt,
    };
    REQUIRE(record::source_container(tiff));
    tiff.planar = 0;
    const auto invalid_tiff = record::source_container(tiff);
    REQUIRE(!invalid_tiff);
    CHECK(invalid_tiff.error().code == core::ErrorCode::invariant);
}
} // namespace docenhance::tests
