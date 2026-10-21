// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/fs/paths.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/request.hpp"
#include "png_depth_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace docenhance::tests {
inline app::Published delivery_record(const app::ProcessRequest& request,
                                      record::Request persistent_request) {
    const auto build = core::build_facts();
    return app::Published{
        .output = request.output_directory() + "/result.png",
        .record_identity = core::ContentIdentity(),
        .record =
            {
                .context = {.identity = std::string(32, 'a'), .recorded = "2026-10-19T00:00:00Z"},
                .build =
                    {
                        .version = std::string{build.version},
                        .platform = std::string{build.platform},
                        .compiler = std::string{build.compiler},
                        .dependency_lock_sha256 = std::string{build.dependency_lock_sha256},
                    },
                .source =
                    {
                        .name = fs::file_name(request.input()),
                        .sha256 = std::string(64, 'c'),
                        .bytes = 71,
                        .container = record::PngContainer{.width = 2, .height = 2},
                    },
                .request = std::move(persistent_request),
                .execution = record::Execution(),
                .assets = std::vector<record::Asset>(),
                .output =
                    {
                        .sha256 = std::string(64, 'd'),
                        .bytes = 71,
                        .width = 2,
                        .height = 2,
                        .depth = docenhance::tests::fixture_png_depth(1),
                        .resolution = std::nullopt,
                    },
            },
        .diagnostics =
            {
                .threads = 1,
                .memory_limit_mib = core::default_memory_limit / (std::size_t{1024} * 1024),
                .charged_peak_bytes = 1,
            },
    };
}
inline void identify_delivery_record(app::Published& value) {
    REQUIRE(record::validate(value.record));
    const auto serialized = record::serialize(value.record);
    REQUIRE(serialized);
    auto identity = fs::identify(std::as_bytes(std::span{*serialized}));
    REQUIRE(identity);
    value.record_identity = std::move(*identity);
}
// A synthetic processing-port observation for delivery tests, never a filesystem publication.
// Derive the record/request/build association and exact serialized record identity for the
// renderer and delivery controls; this fixture does not execute a native publication.
inline app::Published delivery_publication(const app::ProcessRequest& request) {
    REQUIRE(!request.mask_path());
    REQUIRE(!request.psf_path());
    const auto* binary = std::get_if<methods::BinaryOutput>(&request.plan().output);
    REQUIRE(binary != nullptr);
    REQUIRE(std::holds_alternative<methods::FixedThreshold>(binary->method));
    auto persistent_request = record::requested(request.plan(), std::nullopt);
    REQUIRE(persistent_request);
    auto value = delivery_record(request, std::move(*persistent_request));
    value.record.execution.interpretation = {
        .decoded = {.width = 2, .height = 2},
        .output = value.record.output.shape(),
        .orientation = image::Orientation(),
        .rotation = request.plan().rotation,
        .resolution = std::nullopt,
        .assumed_transfer = true,
    };
    methods::FixedFacts facts;
    facts.black_samples = 2;
    facts.white_samples = 2;
    value.record.execution.binarization = facts;
    identify_delivery_record(value);
    return value;
}
} // namespace docenhance::tests
