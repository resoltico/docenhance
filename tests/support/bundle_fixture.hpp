// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "bundle_verify.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/request.hpp"
#include "docenhance/record/source.hpp"
#include "png_depth_fixture.hpp"
#include "png_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace docenhance::tests::bundle_fixture {
inline std::span<const std::byte> bytes(const std::string& value) {
    return std::as_bytes(std::span{value.data(), value.size()});
}
struct WrittenBundle {
    std::string image;
    std::string record;
    host::ExpectedBundle expected;
};
inline WrittenBundle written_bundle() {
    const auto png = make_gray_png({
        .width = 2,
        .height = 2,
        .depth = 1,
        .interlaced = false,
        .samples = std::vector<std::uint8_t>(4, 0),
    });
    std::string image;
    for (const auto value : png) {
        image.push_back(static_cast<char>(value));
    }
    const auto digest = fs::identify(bytes(image));
    REQUIRE(digest);
    const auto method = methods::FixedThreshold::create();
    REQUIRE(method);
    const auto build = core::build_facts();
    const methods::Plan plan{
        .rotation = image::QuarterTurn(),
        .photometric = methods::Photometric(),
        .output = methods::BinaryOutput{.method = *method},
    };
    const auto requested = record::requested(plan, std::nullopt);
    REQUIRE(requested);
    record::RunRecord facts{
        .context = {.identity = std::string(32, 'a'), .recorded = "2026-09-30T00:00:00Z"},
        .build =
            {
                .version = std::string{build.version},
                .platform = std::string{build.platform},
                .compiler = std::string{build.compiler},
                .dependency_lock_sha256 = std::string{build.dependency_lock_sha256},
            },
        .source =
            {
                .name = "source.png",
                .sha256 = std::string(64, 'b'),
                .bytes = 1,
                .container = record::PngContainer{.width = 2, .height = 2},
            },
        .request = *requested,
        .execution = record::Execution(),
        .assets = std::vector<record::Asset>(),
        .output =
            {
                .sha256 = digest->sha256,
                .bytes = digest->bytes,
                .width = 2,
                .height = 2,
                .depth = docenhance::tests::fixture_png_depth(1),
                .resolution = std::nullopt,
            },
    };
    facts.execution.interpretation = {
        .decoded = {.width = 2, .height = 2},
        .output = facts.output.shape(),
        .orientation = image::Orientation(),
        .rotation = image::QuarterTurn(),
        .resolution = std::nullopt,
        .assumed_transfer = true,
    };
    methods::FixedFacts classification;
    classification.black_samples = 4;
    facts.execution.binarization = classification;
    auto record = record::serialize(facts);
    REQUIRE(record);
    const auto identity = fs::identify(bytes(*record));
    REQUIRE(identity);
    return {
        .image = image,
        .record = *record,
        .expected = {.run = facts.context.identity, .record = *identity},
    };
}
} // namespace docenhance::tests::bundle_fixture
