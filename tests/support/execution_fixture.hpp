// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/pipeline/render.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/request.hpp"
#include "docenhance/record/source.hpp"
#include "png_fixture.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace docenhance::tests {
// Real pure rendering provides coherent port observations; this fixture never claims a filesystem
// publication or runs the host. Record and response controls consume the actual eager Facts.
inline app::Published rendering_observation(const app::ProcessRequest& request,
                                            const pipeline::Rendering& rendered,
                                            const core::ContentIdentity& source,
                                            const core::Budget& budget) {
    auto container = record::source_container(rendered.source);
    REQUIRE(container);
    auto selected = record::requested(request.plan(), std::nullopt);
    REQUIRE(selected);
    const auto output = fs::identify(rendered.result.bytes());
    REQUIRE(output);
    const auto build = core::build_facts();
    app::Published value{
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
                        .name = "input.png",
                        .sha256 = source.sha256,
                        .bytes = source.bytes,
                        .container = std::move(*container),
                    },
                .request = std::move(*selected),
                .execution = record::Execution(),
                .assets = std::vector<record::Asset>(),
                .output =
                    {
                        .sha256 = output->sha256,
                        .bytes = output->bytes,
                        .width = rendered.output.shape.width,
                        .height = rendered.output.shape.height,
                        .channels = rendered.output.shape.model,
                        .depth = rendered.output.shape.depth,
                        .profile_embedded = rendered.output.profile_embedded,
                        .resolution = rendered.output.resolution,
                    },
            },
        .diagnostics =
            {
                .threads = 1,
                .memory_limit_mib = core::default_memory_limit / (std::size_t{1024} * 1024),
                .charged_peak_bytes = budget.peak(),
            },
    };
    value.record.execution.interpretation = rendered.interpretation;
    static_cast<methods::StageReports&>(value.record.execution) = rendered.stages;
    const auto serialized = record::serialize(value.record);
    REQUIRE(serialized);
    auto identity = fs::identify(std::as_bytes(std::span{*serialized}));
    REQUIRE(identity);
    value.record_identity = std::move(*identity);
    return value;
}
inline std::vector<std::uint8_t> observation_png(image::Extent extent) {
    std::vector<std::uint8_t> samples{0, 255, 128, 64, 192, 32};
    if (extent != image::Extent{.width = 3, .height = 2}) {
        samples.resize(std::size_t{extent.width} * extent.height);
        for (std::uint32_t y = 0; y < extent.height; ++y) {
            for (std::uint32_t x = 0; x < extent.width; ++x) {
                samples.at((std::size_t{y} * extent.width) + x) =
                    static_cast<std::uint8_t>(((37 * x) + (61 * y)) % 256);
            }
        }
    }
    return make_gray_png({
        .width = extent.width,
        .height = extent.height,
        .depth = 8,
        .interlaced = false,
        .samples = std::move(samples),
    });
}
inline void check_observation_request(const app::ProcessRequest& request, image::Extent extent) {
    REQUIRE(request.input() == "input.png");
    REQUIRE(!request.mask_path());
    REQUIRE(!request.psf_path());
    REQUIRE(extent.width != 0);
    REQUIRE(extent.height != 0);
    REQUIRE(extent.width <= 32);
    REQUIRE(extent.height <= 32);
}
inline pipeline::Input observation_input(core::Budget& budget,
                                         std::span<const std::uint8_t> bytes) {
    auto storage = budget.allocate(bytes.size());
    REQUIRE(storage);
    std::ranges::copy(std::as_bytes(bytes), storage->bytes().begin());
    const auto identity = fs::identify(storage->bytes());
    REQUIRE(identity);
    pipeline::Input input;
    input.encoded().source = std::move(*storage);
    input.encoded().source_identity = *identity;
    return input;
}
inline app::Published observed_publication(const app::Invocation& invocation,
                                           image::Extent extent = {.width = 3, .height = 2}) {
    const auto request = app::prepare_process(invocation);
    REQUIRE(request);
    check_observation_request(*request, extent);
    const auto bytes = observation_png(extent);
    core::Budget budget{core::default_memory_limit};
    auto input = observation_input(budget, bytes);
    const auto identity = input.encoded().source_identity;
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    const auto rendered = pipeline::render(std::move(input), request->plan(), budget, scheduler);
    REQUIRE(rendered);
    REQUIRE(rendered->output.decoded_and_compared);
    return rendering_observation(*request, *rendered, identity, budget);
}
} // namespace docenhance::tests
