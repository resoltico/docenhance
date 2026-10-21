// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/record/codec.hpp"
#include "working_cases.hpp"

#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace docenhance::tests {
TEST_CASE("Working eligibility preserves borrowed samples and ignores mask padding") {
    working_eligibility_cases();
    SUCCEED("Independent working-plane contract passed");
}
TEST_CASE("Working views reject empty geometry mismatches and aliased mask roles") {
    working_view_cases();
    SUCCEED("Independent working-plane contract passed");
}
TEST_CASE("Working eligibility cancellation reaches a wide row without modifying storage") {
    working_cancellation_cases();
    SUCCEED("Independent working-plane contract passed");
}
TEST_CASE("Method report count descriptors reject impossible cross field observations") {
    const methods::PixelCounts counts{
        .eligible = 6,
        .protected_samples = 2,
        .evaluated = 6,
        .changed = 3,
    };
    auto encoded_result = record::Codec::encode(counts);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    CHECK(encoded["protected"] == 2);
    const auto decoded = record::Codec::decode<methods::PixelCounts>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == counts);
    encoded["changed"] = 7;
    CHECK(!record::Codec::decode<methods::PixelCounts>(encoded));
    encoded["changed"] = 0;
    encoded["evaluated"] = true;
    CHECK(!record::Codec::decode<methods::PixelCounts>(encoded));
    CHECK(!methods::validate(methods::PixelCounts{}));
    CHECK(!methods::counts_match(counts, 7));
    const auto invalid = record::Codec::encode(methods::PixelCounts{.eligible = 1, .changed = 1});
    REQUIRE(!invalid);
    CHECK(invalid.error().code == core::ErrorCode::invariant);
    const auto disposition = record::Codec::encode(methods::Disposition::skipped);
    REQUIRE(disposition);
    CHECK(*disposition == "skipped");
    const auto family = record::Codec::encode(methods::Family::binarization);
    REQUIRE(family);
    CHECK(*family == "binarization");
}
} // namespace docenhance::tests
