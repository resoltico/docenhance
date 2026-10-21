// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/morphology_facts.hpp"

#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/record/codec.hpp"
#include "morphology_fact_cases.hpp"
#include "morphology_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace docenhance::tests {
TEST_CASE("Completed I02 facts retain geometry sampling protection and bypass precedence") {
    morphology_fact_cases();
    SUCCEED("Independent completed I02 fact cases passed");
}
TEST_CASE("Completed I02 fields round trip analysis without foreign stage islands") {
    const auto facts = completed_morphology_facts();
    auto encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    CHECK(encoded["method"] == "I02");
    CHECK(encoded["analysis"]["count"] == 256);
    const auto decoded = record::Codec::decode<methods::MorphologyFacts>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == facts);
    CHECK(methods::validate(*decoded, morphology_fact_parameters, morphology_fact_extent));
    encoded["solver"] = nullptr;
    CHECK(!record::Codec::decode<methods::MorphologyFacts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["analysis"]["background_q50"] = 0.1;
    CHECK(!record::Codec::decode<methods::MorphologyFacts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["sigma"] = 3;
    CHECK(!record::Codec::decode<methods::MorphologyFacts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["counts"]["eligible"] = 255;
    encoded["counts"]["protected"] = 1;
    encoded["counts"]["evaluated"] = 255;
    encoded["analysis"]["count"] = 255;
    encoded["min_gain"] = 1.1;
    CHECK(!record::Codec::decode<methods::MorphologyFacts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["analysis"]["analysis_fill"] = 0;
    CHECK(!record::Codec::decode<methods::MorphologyFacts>(encoded));
}
TEST_CASE("Completed I02 contextual wire rejects impossible cap counts and full-field extrema") {
    const auto black = black_morphology_facts();
    auto encoded_result = record::Codec::encode(black);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    const auto baseline = record::Codec::decode<methods::MorphologyFacts>(encoded);
    const methods::MorphologyParameters parameters{.target = 1};
    const image::Extent extent{.width = 4, .height = 4};
    REQUIRE(baseline);
    CHECK(*baseline == black);
    CHECK(methods::validate(*baseline, parameters, extent));
    encoded["gain_capped_samples"] = 15;
    const auto capped = record::Codec::decode<methods::MorphologyFacts>(encoded);
    CHECK((!capped || !methods::validate(*capped, parameters, extent)));
    encoded_result = record::Codec::encode(completed_morphology_facts());
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["analysis"]["target"] = 1;
    encoded["min_gain"] = 1.2;
    encoded["gain_capped_samples"] = 128;
    const auto extrema = record::Codec::decode<methods::MorphologyFacts>(encoded);
    CHECK((!extrema || !methods::validate(*extrema, parameters, morphology_fact_extent)));
    encoded_result = record::Codec::encode(completed_morphology_facts());
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["gain_capped_samples"] = 1;
    const auto quantile = record::Codec::decode<methods::MorphologyFacts>(encoded);
    CHECK((!quantile ||
           !methods::validate(*quantile, morphology_fact_parameters, morphology_fact_extent)));
}
} // namespace docenhance::tests
