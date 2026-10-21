// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/inventory.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/request.hpp"
#include "docenhance/record/source.hpp"
#include "png_depth_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace docenhance::tests {
namespace {
record::RunRecord binary_record() {
    const auto fixed = methods::FixedThreshold::create(0.5);
    REQUIRE(fixed);
    const methods::Plan plan{
        .rotation = image::QuarterTurn(),
        .photometric = methods::Photometric(),
        .output = methods::BinaryOutput{.method = *fixed},
    };
    auto request = record::requested(plan, std::nullopt);
    REQUIRE(request);
    record::RunRecord value{
        .context = {.identity = std::string(32, 'a'), .recorded = "2026-10-19T00:00:00Z"},
        .build =
            {
                .version = "0.9.0",
                .platform = "test-platform",
                .compiler = "test-compiler",
                .dependency_lock_sha256 = std::string(64, 'b'),
            },
        .source =
            {
                .name = "caf\xc3\xa9.png",
                .sha256 = std::string(64, 'c'),
                .bytes = 71,
                .container =
                    record::PngContainer{
                        .width = 2,
                        .height = 2,
                    },
            },
        .request = std::move(*request),
        .execution = record::Execution(),
        .assets = std::vector<record::Asset>(),
        .output =
            {
                .sha256 = std::string(64, 'd'),
                .bytes = 71,
                .width = 2,
                .height = 2,
                .depth = fixture_png_depth(1),
                .resolution = std::nullopt,
            },
    };
    value.execution.interpretation = {
        .decoded = {.width = 2, .height = 2},
        .output = value.output.shape(),
        .orientation = image::Orientation(),
        .rotation = image::QuarterTurn(),
        .resolution = std::nullopt,
        .assumed_transfer = true,
    };
    methods::FixedFacts facts;
    facts.black_samples = 2;
    facts.white_samples = 2;
    value.execution.binarization = facts;
    return value;
}
std::span<const std::byte> record_bytes(const std::string& value) {
    return std::as_bytes(std::span{value});
}
void supply_mask(record::RunRecord& value) {
    value.request.protection =
        record::RequestedProtection{.supplied = {.sha256 = std::string(64, 'e'), .bytes = 100}};
    value.assets.emplace_back(record::ProtectionAsset{
        .sha256 = std::string(64, 'f'),
        .bytes = 80,
        .width = 2,
        .height = 2,
        .protected_samples = 1,
    });
}
} // namespace
TEST_CASE("Wire15 run record has one request and one execution with no stored warnings") {
    const auto value = binary_record();
    REQUIRE(record::validate(value));
    const auto encoding = record::Codec::encode(value);
    REQUIRE(encoding);
    const auto& encoded = *encoding;
    CHECK(encoded.size() == 7);
    CHECK(encoded["record"]["version"] == 15);
    CHECK(encoded["source"].contains("container"));
    CHECK(!encoded["source"].contains("decoding"));
    CHECK(!encoded["request"].contains("operation"));
    CHECK(!encoded["execution"].contains("conversion"));
    CHECK(!encoded.contains("warnings"));
    CHECK(!encoded.contains("publication"));
    CHECK(encoded["request"]["binarization"]["version"] == 2);
    CHECK(encoded["execution"]["illumination"].is_null());
    const auto written = record::serialize(value);
    REQUIRE(written);
    const auto decoded = record::read_record(record_bytes(*written));
    REQUIRE(decoded);
    CHECK(*decoded == value);
    for (const auto version : {0, 14, 16}) {
        auto invalid = encoded;
        invalid["record"]["version"] = version;
        CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    }
    auto invalid = encoded;
    invalid["request"]["operation"] = {{"kind", "binary"}};
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    invalid = encoded;
    invalid["execution"]["illumination"] = {{"progress", "not_started"}};
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
}
TEST_CASE("Wire15 links actual source output geometry and the selected completed family") {
    const auto value = binary_record();
    const auto encoding = record::Codec::encode(value);
    REQUIRE(encoding);
    const auto& encoded = *encoding;
    for (const auto& path : {"source", "output"}) {
        auto invalid = encoded;
        if (std::string{path} == "source") {
            invalid["source"]["container"]["width"] = 3;
        } else {
            invalid["output"]["width"] = 3;
        }
        CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    }
    auto invalid = encoded;
    invalid["execution"]["binarization"]["white_samples"] = 1;
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    invalid = encoded;
    invalid["execution"]["binarization"]["method"] = "B02";
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    invalid = encoded;
    invalid["output"]["profile_embedded"] = true;
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    invalid = encoded;
    invalid["execution"]["interpretation"]["profile_decision"] = "overridden_srgb";
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    invalid["request"]["output"]["profile_policy"] = "srgb";
    CHECK(record::Codec::decode<record::RunRecord>(invalid));
    invalid = encoded;
    invalid["request"]["output"]["profile_policy"] = "srgb";
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    invalid = encoded;
    invalid["source"]["name"] = "directory/input.png";
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    invalid["source"]["name"] = "input\\name.png";
    CHECK(record::Codec::decode<record::RunRecord>(invalid));
}
TEST_CASE("Wire15 mask protects stages while every pixel remains classified") {
    auto value = binary_record();
    supply_mask(value);
    REQUIRE(record::validate(value));
    const auto encoding = record::Codec::encode(value);
    REQUIRE(encoding);
    const auto& encoded = *encoding;
    CHECK(encoded["assets"].size() == 1);
    CHECK(encoded["assets"][0]["role"] == "protection_mask");
    CHECK(encoded["assets"][0]["frame"] == "metadata_oriented_source");
    CHECK(record::Codec::decode<record::RunRecord>(encoded));
    const record::Json replacements{
        {"width", 3},        {"protected_samples", 5},
        {"frame", "output"}, {"path", "../protect-mask.png"},
        {"role", "unknown"},
    };
    for (const auto& [key, replacement] : replacements.items()) {
        auto invalid = encoded;
        invalid["assets"][0][key] = replacement;
        CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    }
    auto invalid = encoded;
    invalid["assets"].push_back(invalid["assets"][0]);
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    invalid = encoded;
    invalid["request"]["protection"] = nullptr;
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    invalid = encoded;
    invalid["assets"] = record::Json::array();
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    invalid = encoded;
    invalid["assets"].push_back({
        {"role", "psf_kernel"},
        {"path", "assets/psf.png"},
        {"sha256", std::string(64, 'a')},
        {"bytes", 71},
    });
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
}
TEST_CASE("Wire15 artifact inventory is closed and includes the record once") {
    auto value = binary_record();
    supply_mask(value);
    auto inventory = record::inventory(value);
    REQUIRE(inventory);
    REQUIRE(inventory->size() == 2);
    auto present = *inventory;
    present.push_back({
        .name = record::record_name,
        .identity =
            {
                .sha256 = std::string(64, 'a'),
                .bytes = 1000,
            },
    });
    const std::vector<std::string> directories{"assets"};
    CHECK(record::agrees(value, present, directories));
    auto invalid = present;
    invalid.pop_back();
    CHECK(!record::agrees(value, invalid, directories));
    invalid = present;
    invalid.push_back(present.front());
    CHECK(!record::agrees(value, invalid, directories));
    invalid = present;
    invalid.front().identity.bytes++;
    CHECK(!record::agrees(value, invalid, directories));
    CHECK(!record::agrees(value, present, std::vector<std::string>{"foreign"}));
}
TEST_CASE("Wire15 raw record reader keeps finite pre-DOM limits and duplicate key refusal") {
    const auto written = record::serialize(binary_record());
    REQUIRE(written);
    auto duplicate = *written;
    duplicate.insert(1, R"("\u0073ource":{},)");
    CHECK(!record::read_record(record_bytes(duplicate)));
    CHECK(!record::read_record(record_bytes(std::string(record::record_max_bytes + 1, ' '))));
    CHECK(!record::read_record(record_bytes(std::string(record::record_max_depth + 1, '['))));
    auto encoding = record::Codec::encode(binary_record());
    REQUIRE(encoding);
    auto invalid = std::move(*encoding);
    invalid["record"]["recorded"] = "2026-02-30T00:00:00Z";
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
    invalid["record"]["recorded"] = "2026-10-19T00:00:00Z";
    invalid["source"]["sha256"] = std::string(64, 'A');
    CHECK(!record::Codec::decode<record::RunRecord>(invalid));
}
TEST_CASE("Record serialization propagates inconsistent typed facts as Invariant") {
    auto value = binary_record();
    REQUIRE(record::serialize(value));
    value.output.width = 3;
    const auto inconsistent_geometry = record::serialize(value);
    REQUIRE(!inconsistent_geometry);
    CHECK(inconsistent_geometry.error().code == core::ErrorCode::invariant);
    value = binary_record();
    value.source.name = std::string(1, static_cast<char>(0xff));
    const auto malformed_identity = record::serialize(value);
    REQUIRE(!malformed_identity);
    CHECK(malformed_identity.error().code == core::ErrorCode::invariant);
}
} // namespace docenhance::tests
