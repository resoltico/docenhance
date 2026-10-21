// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/stage_reports.hpp"

#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/record/codec.hpp"
#include "stage_report_cases.hpp"
#include "stage_report_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace docenhance::tests {
TEST_CASE("Closed stage reports retain a shared canvas and classification population") {
    stage_report_cases();
    SUCCEED("Independent closed stage report cases passed");
}
TEST_CASE("Closed stage success fields require nulls while completed failure fields are sparse") {
    const auto stages = selected_stage_reports();
    auto encoded_result = record::Codec::encode(stages);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    CHECK(encoded.size() == 6);
    CHECK(encoded["illumination"].is_null());
    CHECK(encoded["denoising"]["method"] == "D01");
    const auto decoded = record::Codec::decode<methods::StageReports>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == stages);
    encoded.erase("illumination");
    CHECK(!record::Codec::decode<methods::StageReports>(encoded));
    methods::CompletedStages completed;
    completed.denoising = stages.denoising;
    encoded_result = record::Codec::encode(completed);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    CHECK(encoded.size() == 1);
    const auto retained = record::Codec::decode<methods::CompletedStages>(encoded);
    REQUIRE(retained);
    CHECK(*retained == completed);
    encoded["illumination"] = nullptr;
    CHECK(!record::Codec::decode<methods::CompletedStages>(encoded));
    encoded_result = record::Codec::encode(completed);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    encoded["geometry"] = nullptr;
    CHECK(!record::Codec::decode<methods::CompletedStages>(encoded));
    methods::FixedFacts binary;
    binary.black_samples = 512;
    encoded_result = record::Codec::encode(stages);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    const auto binary_encoded = record::Codec::encode(binary);
    REQUIRE(binary_encoded);
    encoded["illumination"] = *binary_encoded;
    CHECK(!record::Codec::decode<methods::StageReports>(encoded));
}
} // namespace docenhance::tests
