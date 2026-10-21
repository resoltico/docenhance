// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/request.hpp"
#include "docenhance/record/source.hpp"
#include "png_depth_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace docenhance::tests::record_fixture {
inline constexpr std::string_view sample_digest =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
inline constexpr std::string_view other_digest =
    "3f79bb7b435b05321651daefd374cdc681dc06faa65e374e38337b88ca046dea";
inline record::RunRecord binarized_record() {
    const auto method = methods::Sauvola::create();
    REQUIRE(method);
    const methods::Plan plan{
        .rotation = image::QuarterTurn(),
        .photometric = methods::Photometric(),
        .output = methods::BinaryOutput{.method = *method},
    };
    auto request = record::requested(plan, std::nullopt);
    REQUIRE(request);
    record::RunRecord value{
        .context =
            {
                .identity = "0123456789abcdef0123456789abcdef",
                .recorded = "2026-09-28T00:00:00Z",
            },
        .build =
            {
                .version = "0.3.0",
                .platform = "Darwin-arm64",
                .compiler = "AppleClang 21",
                .dependency_lock_sha256 = std::string{other_digest},
            },
        .source =
            {
                .name = "page.png",
                .sha256 = std::string{sample_digest},
                .bytes = 1024,
                .container = record::PngContainer{.width = 100, .height = 50},
            },
        .request = std::move(*request),
        .execution = record::Execution(),
        .assets = std::vector<record::Asset>(),
        .output =
            {
                .sha256 = std::string{other_digest},
                .bytes = 2048,
                .width = 100,
                .height = 50,
                .depth = docenhance::tests::fixture_png_depth(1),
                .resolution = std::nullopt,
            },
    };
    value.execution.interpretation = {
        .decoded = {.width = 100, .height = 50},
        .output = value.output.shape(),
        .orientation = image::Orientation(),
        .rotation = image::QuarterTurn(),
        .resolution = std::nullopt,
        .assumed_transfer = true,
    };
    methods::SauvolaFacts facts;
    facts.black_samples = 2500;
    facts.white_samples = 2500;
    value.execution.binarization = facts;
    REQUIRE(record::validate(value));
    return value;
}
} // namespace docenhance::tests::record_fixture
