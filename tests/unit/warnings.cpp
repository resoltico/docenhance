// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/warnings.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/report/warnings.hpp"
#include "png_depth_fixture.hpp"
#include "warning_cases.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <stdexcept>

namespace docenhance::tests {
TEST_CASE("Derived warnings retain method conversion and transform meanings") {
    warning_cases();
    SUCCEED("Independent warning projections passed");
}
TEST_CASE("Every closed warning code retains a nonempty human explanation") {
    for (const auto& [code, name] : core::EnumNames<core::Warning>::list) {
        CHECK(!name.empty());
        const auto explanation = report::warning_explanation(code);
        REQUIRE(explanation);
        CHECK((explanation && !explanation->empty()));
    }
}
TEST_CASE(
    "Run warning aggregation deduplicates profile causes and orders actual partial TV context") {
    app::ProcessFailure failure{
        .error = {.code = core::ErrorCode::resource, .message = "Injected partial TV refusal"},
        .failed_stage = methods::Family::denoising,
    };
    failure.warning_context.interpretation = image::InterpretationReport{
        .decoded = {.width = 2, .height = 2, .model = image::SampleModel::rgb},
        .output = {.width = 2, .height = 2, .depth = fixture_png_depth(1)},
        .orientation = image::Orientation(),
        .rotation = image::QuarterTurn(),
        .resolution = std::nullopt,
        .assumed_transfer = true,
        .assumed_primaries = true,
    };
    failure.warning_context.observed_tvl1_stop = methods::Tvl1Stop::iteration_limit;
    REQUIRE(image::validate(*failure.warning_context.interpretation));
    REQUIRE(methods::validate(failure.completed));
    const report::RunWarnings expected{
        core::Warning::profile_assumed,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        core::Warning::tv_iteration_limit,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
    };
    CHECK(report::failure_warnings(failure) == expected);
    CHECK(std::ranges::count_if(report::failure_warnings(failure),
                                [](auto value) { return value.has_value(); }) == 2);
    CHECK(core::warning_projection_capacity == 4);
}
TEST_CASE(
    "Partial restoration warnings retain entry and resolved centroid without a completed report") {
    app::ProcessFailure failure{
        .error = {.code = core::ErrorCode::resource, .message = "Injected restoration refusal"},
        .failed_stage = methods::Family::restoration,
    };
    const auto rotation = image::QuarterTurn::from_degrees(90);
    REQUIRE(rotation.has_value());
    if (!rotation.has_value()) {
        throw std::logic_error("Required test rotation is absent");
    }
    failure.warning_context.interpretation = image::InterpretationReport{
        .decoded = {.width = 2, .height = 2},
        .output =
            {
                .width = 2,
                .height = 2,
                .depth = image::PngDepth(),
            },
        .orientation = image::Orientation(),
        .rotation = *rotation,
        .resolution = std::nullopt,
    };
    failure.warning_context.restoration_entered = true;
    failure.warning_context.restoration_after_transform = true;
    failure.warning_context.resolved_psf_centroid = std::array{0.25, 0.25};
    REQUIRE(image::validate(*failure.warning_context.interpretation));
    const report::RunWarnings expected{
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        core::Warning::restoration_inference,
        core::Warning::psf_after_transform,
        core::Warning::psf_off_center,
        std::nullopt,
    };
    CHECK(!failure.completed.restoration);
    CHECK(report::failure_warnings(failure) == expected);
    const app::ProcessFailure unstarted{
        .error = {.code = core::ErrorCode::argument, .message = "Unstarted admission refusal"},
    };
    CHECK(report::failure_warnings(unstarted) == report::RunWarnings{});
}
} // namespace docenhance::tests
