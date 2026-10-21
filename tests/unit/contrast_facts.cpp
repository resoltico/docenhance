// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/contrast_facts.hpp"

#include "contrast_fact_cases.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/record/codec.hpp"

#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace docenhance::tests {
TEST_CASE("Completed contrast facts retain method-specific no-op and active invariants") {
    contrast_fact_cases();
    SUCCEED("Independent contrast fact cases passed");
}
TEST_CASE("Completed contrast fields round trip real per-method values") {
    const methods::GammaFacts gamma{
        .disposition = methods::Disposition::applied,
        .reason = methods::GammaReason::none,
        .counts = {.eligible = 8, .evaluated = 8, .changed = 3},
    };
    auto encoded_gamma_result = record::Codec::encode(gamma);
    REQUIRE(encoded_gamma_result);
    const auto encoded_gamma = std::move(*encoded_gamma_result);
    CHECK(encoded_gamma["method"] == "C02");
    const auto decoded_gamma = record::Codec::decode<methods::GammaFacts>(encoded_gamma);
    REQUIRE(decoded_gamma);
    CHECK(*decoded_gamma == gamma);
    const methods::LevelsFacts levels{
        .reason = methods::LevelsReason::zero_blend,
        .counts = {.eligible = 8},
    };
    auto encoded_levels_result = record::Codec::encode(levels);
    REQUIRE(encoded_levels_result);
    const auto encoded_levels = std::move(*encoded_levels_result);
    CHECK(encoded_levels["method"] == "C01");
    CHECK(encoded_levels["levels"].is_null());
    const auto decoded_levels = record::Codec::decode<methods::LevelsFacts>(encoded_levels);
    REQUIRE(decoded_levels);
    CHECK(*decoded_levels == levels);
    const methods::LevelsFacts active_levels{
        .disposition = methods::Disposition::applied,
        .reason = methods::LevelsReason::none,
        .counts = {.eligible = 8, .evaluated = 8, .changed = 3},
        .levels = methods::LevelsRange{.low = 0.1, .high = 0.9},
    };
    auto encoded_active_result = record::Codec::encode(active_levels);
    REQUIRE(encoded_active_result);
    auto encoded_active = std::move(*encoded_active_result);
    CHECK(encoded_active["levels"]["low"] == 0.1);
    CHECK(encoded_active["levels"]["high"] == 0.9);
    const auto decoded_active = record::Codec::decode<methods::LevelsFacts>(encoded_active);
    REQUIRE(decoded_active);
    CHECK(*decoded_active == active_levels);
    encoded_active["levels"]["low"] = 0.9;
    encoded_active["levels"]["high"] = 0.1;
    CHECK(!record::Codec::decode<methods::LevelsFacts>(encoded_active));
    const methods::ClaheFacts clahe{
        .reason = methods::ClaheReason::zero_blend,
        .counts = {.eligible = 8},
    };
    const auto encoded_clahe = record::Codec::encode(clahe);
    REQUIRE(encoded_clahe);
    const auto decoded_clahe = record::Codec::decode<methods::ClaheFacts>(*encoded_clahe);
    REQUIRE(decoded_clahe);
    CHECK(*decoded_clahe == clahe);
}
TEST_CASE("Completed contrast wire refuses foreign methods lifecycle and unknown reasons") {
    const methods::LevelsFacts levels{
        .reason = methods::LevelsReason::zero_blend,
        .counts = {.eligible = 8},
    };
    auto encoded_result = record::Codec::encode(levels);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    encoded["method"] = "C02";
    CHECK(!record::Codec::decode<methods::GammaFacts>(encoded));
    encoded["method"] = "C01";
    encoded["progress"] = "completed";
    CHECK(!record::Codec::decode<methods::LevelsFacts>(encoded));
    encoded.erase("progress");
    encoded["reason"] = "processing_failure";
    CHECK(!record::Codec::decode<methods::LevelsFacts>(encoded));
    encoded["reason"] = "zero_blend";
    encoded.erase("levels");
    CHECK(!record::Codec::decode<methods::LevelsFacts>(encoded));
}
TEST_CASE("Levels baseline refuses an ordered narrow active claim at the actual wire owner") {
    const methods::LevelsFacts active{
        .disposition = methods::Disposition::applied,
        .reason = methods::LevelsReason::none,
        .counts = {.eligible = 8, .evaluated = 8, .changed = 3},
        .levels = methods::LevelsRange{.low = 0.1, .high = 0.9},
    };
    const auto encoded = record::Codec::encode(active);
    REQUIRE(encoded);
    const auto decoded = record::Codec::decode<methods::LevelsFacts>(*encoded);
    REQUIRE(decoded);
    CHECK(*decoded == active);
    CHECK(methods::validate(active, methods::LevelsParameters{}));
    auto narrow = active;
    narrow.levels = methods::LevelsRange{.low = 0.5, .high = 0.5000005};
    REQUIRE(narrow.levels);
    if (narrow.levels) {
        CHECK(methods::validate(*narrow.levels));
    }
    CHECK(!methods::validate(narrow, methods::LevelsParameters{}));
    auto forged_result = record::Codec::encode(active);
    REQUIRE(forged_result);
    auto forged = std::move(*forged_result);
    forged["levels"]["low"] = 0.5;
    forged["levels"]["high"] = 0.5000005;
    CHECK(!record::Codec::decode<methods::LevelsFacts>(forged));
}
TEST_CASE("Levels baseline admits the same narrow bounds as a completed fitted no-op") {
    const methods::LevelsFacts flat{
        .reason = methods::LevelsReason::insufficient_dynamic_range,
        .counts = {.eligible = 8},
        .levels = methods::LevelsRange{.low = 0.5, .high = 0.5000005},
    };
    CHECK(methods::validate(flat, methods::LevelsParameters{}));
    const auto encoded = record::Codec::encode(flat);
    REQUIRE(encoded);
    const auto decoded = record::Codec::decode<methods::LevelsFacts>(*encoded);
    REQUIRE(decoded);
    CHECK(*decoded == flat);
    if (decoded) {
        CHECK(methods::validate(*decoded, methods::LevelsParameters{}));
    }
}
} // namespace docenhance::tests
