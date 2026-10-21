// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/binary_facts.hpp"

#include "binary_fact_cases.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/schema.hpp"

#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace docenhance::tests {
TEST_CASE("Completed binary facts preserve tallies threshold classes and terminal semantics") {
    binary_fact_cases();
    SUCCEED("Independent classification fact cases passed");
}
TEST_CASE("Completed binary descriptors flatten shared counts and retain closed literals") {
    methods::OtsuFacts facts;
    facts.black_samples = 3;
    facts.white_samples = 5;
    facts.threshold_bin = 100;
    auto encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    CHECK(encoded["method"] == "B01");
    CHECK(encoded["black_samples"] == 3);
    CHECK(encoded["threshold_bin"] == 100);
    const auto decoded = record::Codec::decode<methods::OtsuFacts>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == facts);
    auto schema_result = record::Schema::of<methods::OtsuFacts>();
    REQUIRE(schema_result);
    const auto schema = std::move(*schema_result);
    CHECK(schema["properties"]["disposition"]["const"] == "applied");
    encoded["disposition"] = "no_change";
    CHECK(!record::Codec::decode<methods::OtsuFacts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["protected_samples"] = 1;
    CHECK(!record::Codec::decode<methods::OtsuFacts>(encoded));
    methods::SauvolaFacts local;
    local.white_samples = 8;
    const auto local_encoded = record::Codec::encode(local);
    REQUIRE(local_encoded);
    const auto local_decoded = record::Codec::decode<methods::SauvolaFacts>(*local_encoded);
    REQUIRE(local_decoded);
    CHECK(*local_decoded == local);
    methods::FixedFacts fixed;
    fixed.black_samples = 8;
    const auto fixed_encoded = record::Codec::encode(fixed);
    REQUIRE(fixed_encoded);
    const auto fixed_decoded = record::Codec::decode<methods::FixedFacts>(*fixed_encoded);
    REQUIRE(fixed_decoded);
    CHECK(*fixed_decoded == fixed);
}
} // namespace docenhance::tests
