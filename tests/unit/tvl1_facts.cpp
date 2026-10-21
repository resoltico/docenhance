// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/tvl1_facts.hpp"

#include "docenhance/methods/parameters.hpp"
#include "docenhance/record/codec.hpp"
#include "tvl1_fact_cases.hpp"

#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace docenhance::tests {
TEST_CASE("Completed TV facts preserve solver stopping counts and no-op boundaries") {
    tvl1_fact_cases();
    SUCCEED("Independent completed TV fact cases passed");
}
TEST_CASE("Completed TV descriptors round trip solution facts and refuse foreign native islands") {
    const auto facts = completed_tv_facts();
    auto encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    CHECK(encoded["method"] == "D02");
    CHECK(encoded["solver"]["iterations"] == 30);
    CHECK(encoded["solver"]["stop"] == "tolerance_met");
    const auto decoded = record::Codec::decode<methods::Tvl1Facts>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == facts);
    CHECK(methods::validate(*decoded, methods::Tvl1Parameters{}));
    encoded["native_h"] = 771;
    CHECK(!record::Codec::decode<methods::Tvl1Facts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["solver"]["stop"] = "processing_failure";
    CHECK(!record::Codec::decode<methods::Tvl1Facts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["solver"]["objective_start"] = 0;
    CHECK(!record::Codec::decode<methods::Tvl1Facts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["solver"]["objective_start"] = 1e300;
    CHECK(!record::Codec::decode<methods::Tvl1Facts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["solver"]["objective_end"] = 1e300;
    CHECK(!record::Codec::decode<methods::Tvl1Facts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["disposition"] = "no_change";
    encoded["reason"] = "no_effect";
    encoded["counts"]["changed"] = 0;
    encoded["corrected_samples"] = 0;
    CHECK(!record::Codec::decode<methods::Tvl1Facts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["solver"]["objective_end"] = -1;
    CHECK(!record::Codec::decode<methods::Tvl1Facts>(encoded));
}
} // namespace docenhance::tests
