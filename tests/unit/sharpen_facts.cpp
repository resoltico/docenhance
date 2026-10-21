// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/sharpen_facts.hpp"

#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/record/codec.hpp"
#include "sharpen_fact_cases.hpp"

#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace docenhance::tests {
TEST_CASE("Completed unsharp facts preserve context corrections and identity invariants") {
    sharpen_fact_cases();
    SUCCEED("Independent unsharp fact cases passed");
}
TEST_CASE("Completed unsharp fields round trip raw extrema and reject foreign observations") {
    const methods::UnsharpFacts facts{
        .disposition = methods::Disposition::applied,
        .reason = methods::UnsharpReason::none,
        .counts = {.eligible = 8, .evaluated = 8, .changed = 2},
        .context_samples = 8,
        .corrected_samples = 3,
        .pre_clamp = methods::ExcursionRange{.low = -0.1, .high = 1.1},
        .clipped_low_samples = 1,
        .clipped_high_samples = 1,
    };
    auto encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    CHECK(encoded["method"] == "S01");
    CHECK(encoded["pre_clamp"]["low"] == -0.1);
    CHECK(encoded["pre_clamp"]["high"] == 1.1);
    const auto decoded = record::Codec::decode<methods::UnsharpFacts>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == facts);
    encoded["pre_clamp"]["low"] = 1.2;
    CHECK(!record::Codec::decode<methods::UnsharpFacts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["native_calls"] = 1;
    CHECK(!record::Codec::decode<methods::UnsharpFacts>(encoded));
}
} // namespace docenhance::tests
