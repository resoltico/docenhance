// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/restoration_facts.hpp"

#include "docenhance/methods/parameters.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "restoration_fact_cases.hpp"
#include "restoration_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <utility>

namespace docenhance::tests {
TEST_CASE("Completed R01 facts preserve PSF transform excursion and bypass invariants") {
    restoration_fact_cases();
    SUCCEED("Independent completed R01 fact cases passed");
}
TEST_CASE(
    "Completed R01 fields round trip scalar PSF facts without coefficients or supplied paths") {
    const auto facts = completed_wiener_facts();
    auto encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    CHECK(encoded["method"] == "R01");
    CHECK(encoded["psf"]["kind"] == "gaussian");
    const auto decoded = record::Codec::decode<methods::WienerFacts>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == facts);
    const methods::WienerParameters parameters;
    CHECK(methods::validate(*decoded, parameters, restoration_fact_extent));
    encoded["psf"]["coefficients"] = record::Json::array({1});
    CHECK(!record::Codec::decode<methods::WienerFacts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["psf"]["path"] = "private/kernel.png";
    CHECK(!record::Codec::decode<methods::WienerFacts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["native_calls"] = 3;
    CHECK(!record::Codec::decode<methods::WienerFacts>(encoded));
    auto kernel = facts;
    kernel.psf.kind = methods::PsfKind::kernel;
    encoded_result = record::Codec::encode(kernel);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    CHECK(!encoded.dump().contains("private/kernel.png"));
}
TEST_CASE("Completed R01 tiny blend wire retains raw excursions without rounded corrections") {
    const auto facts = tiny_blend_wiener_facts();
    auto encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    const auto decoded = record::Codec::decode<methods::WienerFacts>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == facts);
    CHECK(decoded->raw_low_samples == 1);
    CHECK(decoded->corrected_samples == 0);
    const methods::WienerParameters tiny{.blend = std::numeric_limits<double>::denorm_min()};
    CHECK(methods::validate(*decoded, tiny, restoration_fact_extent));
    encoded["blended_low_samples"] = 1;
    CHECK(!record::Codec::decode<methods::WienerFacts>(encoded));
}
TEST_CASE("Completed R01 kernel edge scalar wire preserves actual normalized moments") {
    methods::WienerFacts facts{
        .reason = methods::WienerReason::zero_blend,
        .counts = {.eligible = 128},
        .psf = edge_psf_facts(129, 129, false, true),
    };
    auto encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    const auto decoded = record::Codec::decode<methods::WienerFacts>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == facts);
    const methods::WienerParameters parameters{
        .psf = methods::Kernel{},
        .blend = 0,
    };
    CHECK(methods::validate(*decoded, parameters, restoration_fact_extent));
    facts.psf = edge_psf_facts(3, 3, true, true);
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["psf"]["centroid_x"] = 1.5;
    CHECK(!record::Codec::decode<methods::WienerFacts>(encoded));
}
} // namespace docenhance::tests
