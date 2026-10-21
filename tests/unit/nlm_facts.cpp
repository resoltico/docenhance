// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/nlm_facts.hpp"

#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/record/codec.hpp"
#include "nlm_fact_cases.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <utility>

namespace docenhance::tests {
TEST_CASE("Completed NLM facts use one table owner and retain parameter agreement") {
    nlm_cases();
    SUCCEED("Independent NLM table and fact cases passed");
}
TEST_CASE("Completed NLM fields round trip without native telemetry or request duplication") {
    const auto method = methods::Nlm::create();
    REQUIRE(method);
    const methods::NlmFacts facts{
        .disposition = methods::Disposition::applied,
        .reason = methods::NlmReason::none,
        .counts = {.eligible = 441, .evaluated = 441, .changed = 100},
        .corrected_samples = 120,
        .weight_table_entries = methods::nlm_weight_entries(*method),
    };
    auto encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    CHECK(encoded["method"] == "D01");
    const auto decoded = record::Codec::decode<methods::NlmFacts>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == facts);
    for (const std::uint32_t count : {1U, 40000000U}) {
        encoded["weight_table_entries"] = count;
        const auto broad = record::Codec::decode<methods::NlmFacts>(encoded);
        REQUIRE(broad);
        CHECK(!methods::validate(*broad, methods::NlmParameters{}));
    }
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["native_h"] = 771;
    CHECK(!record::Codec::decode<methods::NlmFacts>(encoded));
}
TEST_CASE("Completed active NLM no-effect wire retains its real table and evaluation") {
    const auto method = methods::Nlm::create();
    REQUIRE(method);
    const methods::NlmFacts facts{
        .reason = methods::NlmReason::no_effect,
        .counts = {.eligible = 441, .evaluated = 441},
        .weight_table_entries = methods::nlm_weight_entries(*method),
    };
    auto encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    CHECK(encoded["disposition"] == "no_change");
    CHECK(encoded["reason"] == "no_effect");
    CHECK(encoded["weight_table_entries"] == 113337);
    const auto decoded = record::Codec::decode<methods::NlmFacts>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == facts);
    CHECK(methods::validate(*decoded, methods::NlmParameters{}));
    encoded["weight_table_entries"] = 0;
    CHECK(!record::Codec::decode<methods::NlmFacts>(encoded));
    encoded_result = record::Codec::encode(facts);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["counts"]["evaluated"] = 0;
    CHECK(!record::Codec::decode<methods::NlmFacts>(encoded));
}
} // namespace docenhance::tests
