// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/surface_facts.hpp"

#include "docenhance/image/geometry.hpp"
#include "docenhance/record/codec.hpp"
#include "surface_fact_cases.hpp"
#include "surface_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace docenhance::tests {
TEST_CASE("Completed I01 facts preserve branch decisions sampling and converged observations") {
    surface_fact_cases();
    SUCCEED("Independent completed I01 fact cases passed");
}
TEST_CASE("Completed I01 wire preserves null predicates and refuses foreign or incomplete facts") {
    const auto facts = completed_surface_facts();
    auto encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    CHECK(encoded["method"] == "I01");
    CHECK(encoded["auto_predicates"]["coverage"] == true);
    const auto decoded = record::Codec::decode<methods::SurfaceFacts>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == facts);
    CHECK(methods::validate(*decoded, surface_fact_parameters, surface_fact_extent));
    encoded["analysis"] = nullptr;
    CHECK(!record::Codec::decode<methods::SurfaceFacts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["measurements"]["variation"] = 0.5;
    CHECK(!record::Codec::decode<methods::SurfaceFacts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["solver"]["tolerance"] = 1e300;
    CHECK(!record::Codec::decode<methods::SurfaceFacts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["auto_predicates"]["dark_fraction"] = nullptr;
    CHECK(!record::Codec::decode<methods::SurfaceFacts>(encoded));
    encoded_result = record::Codec::encode(singleton_surface_facts());
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    const auto singleton = record::Codec::decode<methods::SurfaceFacts>(encoded);
    REQUIRE(singleton);
    CHECK(*singleton == singleton_surface_facts());
    const image::Extent singleton_extent{.width = 1, .height = 1};
    CHECK(methods::validate(*singleton, surface_fact_parameters, singleton_extent));
}
} // namespace docenhance::tests
