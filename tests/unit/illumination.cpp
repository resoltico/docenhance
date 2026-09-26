// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/methods/illumination.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/surface.hpp"
#include "linear_fixture.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
namespace docenhance::tests {
namespace {
constexpr std::size_t surface_budget = std::size_t{32} * 1024 * 1024;
constexpr image::Extent page{.width = 32, .height = 16};
constexpr double paper = 0.5;
constexpr std::uint32_t cell = 8;
methods::Surface options(bool automatic = false) {
    return methods::Surface::create({
                                        .mode = automatic ? methods::SurfaceMode::automatic
                                                          : methods::SurfaceMode::explicit_surface,
                                        .cell = cell,
                                    })
        .value();
}
constexpr double dark_paper = 0.8;
// Paper with 0.05 dark content: a 2 x 2 block of cells, or every cell outside the first column.
void paint_dark(LinearFixture& source, bool everywhere) {
    constexpr double ink = 0.05;
    source.fill(dark_paper);
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        for (std::uint32_t x = 0; x < source.extent().width; ++x) {
            const auto column = x / cell;
            const auto row = y / cell;
            const bool dark = everywhere ? column != 0 : column < 2 && row >= 1 && row < 3;
            if (dark) {
                std::ranges::fill(source.row(y).subspan(std::size_t{x} * image::rgb_channels,
                                                        image::rgb_channels),
                                  ink);
            }
        }
    }
}
} // namespace
TEST_CASE("I01 factory and application admission reject contradictions before I/O",
          "[surface][app]") {
    static_assert(!std::is_default_constructible_v<methods::Surface>);
    auto params = options().parameters();
    params.strength = std::numeric_limits<double>::quiet_NaN();
    CHECK(!methods::Surface::create(params));
    params = options().parameters();
    params.cell = 1;
    CHECK(!methods::Surface::create(params));
    contract::Invocation request{
        .command = contract::Command::process,
        .subject = "input.png",
        .output_directory = "out",
    };
    request.illumination = "surface";
    REQUIRE(app::prepare_process(request));
    request.background_cell = "";
    CHECK(!app::prepare_process(request));
    request.background_cell = "008";
    REQUIRE(app::prepare_process(request));
    request.illumination = "off";
    CHECK(!app::prepare_process(request));
    request.background_cell.reset();
    request.protect_mask = "mask.png";
    REQUIRE(app::prepare_process(request));
    request.output_mode = "bw";
    CHECK(!app::prepare_process(request));
}
TEST_CASE("Disabled algebra and all-protected input avoid fit allocation exactly",
          "[surface][identity]") {
    core::Budget input_budget{surface_budget};
    LinearFixture source{input_budget, page};
    source.fill(paper);
    auto mask = image::Plane<std::uint8_t>::allocate(input_budget, page.width, page.height).value();
    std::ranges::fill(mask.view().storage(), 1);
    core::Budget empty_budget{0};
    methods::IlluminationReport report;
    auto protected_model = methods::SurfaceModel::prepare({source, mask.view().as_const()},
                                                          options(), empty_budget, {}, report);
    REQUIRE(protected_model);
    CHECK(!protected_model->active());
    CHECK(report.reason == methods::SurfaceReason::no_eligible_samples);
    CHECK(report.protected_samples == std::uint64_t{page.width} * page.height);
    auto params = options().parameters();
    params.strength = 0;
    auto const zero = methods::Surface::create(params).value();
    auto model = methods::SurfaceModel::prepare({source, {}}, zero, empty_budget, {}, report);
    REQUIRE(model);
    CHECK(!model->active());
    CHECK(empty_budget.used() == 0);
    std::array<double, image::rgb_channels> pixel{paper, paper, paper};
    REQUIRE(model->apply({}, pixel, {}, report, {}));
    CHECK(pixel.front() == paper);
}
TEST_CASE("I01 measurement, fit, interpolation and application preserve protected samples",
          "[surface]") {
    core::Budget budget{surface_budget};
    LinearFixture source{budget, page};
    source.fill(paper);
    auto mask = image::Plane<std::uint8_t>::allocate(budget, page.width, page.height).value();
    std::ranges::fill(mask.view().storage(), 0);
    mask.view().row(0).front() = 1;
    constexpr double protected_dark = 0.01;
    std::ranges::fill(source.row(0).first(image::rgb_channels), protected_dark);
    auto params = options().parameters();
    params.strength = 1;
    params.max_gain = 2;
    params.target = 1;
    const auto method = methods::Surface::create(params).value();
    const auto held = budget.used();
    {
        methods::IlluminationReport report;
        auto model = methods::SurfaceModel::prepare({source, mask.view().as_const()}, method,
                                                    budget, {}, report);
        REQUIRE(model);
        REQUIRE(model->active());
        CHECK(report.protected_samples == 1);
        REQUIRE(report.solver);
        if (report.solver) {
            CHECK(report.solver->residual <= report.solver->tolerance);
        }
        constexpr double tolerance = 1e-10;
        CHECK(std::abs(model->background(0, 0).value() - paper) < tolerance);
        CHECK(std::abs(model->background(page.width - 1, page.height - 1).value() - paper) <
              tolerance);
        auto const pixels =
            source.row(0); // Internal test destination is explicitly independent of fitting now.
        REQUIRE(model->apply({}, pixels, mask.view().as_const(), report, {}));
        CHECK(pixels.front() == protected_dark);
        CHECK(pixels.subspan(image::rgb_channels, 1).front() > paper);
        CHECK(report.evaluated_samples == page.width - 1);
        CHECK(report.status == methods::SurfaceStatus::applied);
    }
    CHECK(budget.used() == held);
}
TEST_CASE("I01 automatic skip and explicit inapplicability are not numerical failure",
          "[surface][auto]") {
    core::Budget budget{surface_budget};
    LinearFixture source{budget, page};
    source.fill(paper);
    methods::IlluminationReport report;
    {
        auto skipped =
            methods::SurfaceModel::prepare({source, {}}, options(true), budget, {}, report);
        REQUIRE(skipped);
        CHECK(!skipped->active());
        CHECK(report.status == methods::SurfaceStatus::skipped);
        CHECK(report.reason == methods::SurfaceReason::automatic_predicates);
        REQUIRE(report.predicates.at(3).has_value());
        CHECK(report.predicates.at(3) == false);
    }
    source.fill(0);
    auto refused = methods::SurfaceModel::prepare({source, {}}, options(), budget, {}, report);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::method_inapplicable);
    auto skipped = methods::SurfaceModel::prepare({source, {}}, options(true), budget, {}, report);
    REQUIRE(skipped);
    CHECK(report.status == methods::SurfaceStatus::skipped);
}
TEST_CASE("I01 resource refusal and phase cancellation refund scratch", "[surface][resource]") {
    core::Budget input_budget{surface_budget};
    LinearFixture source{input_budget, page};
    source.fill(paper);
    methods::IlluminationReport report;
    core::Budget empty{0};
    auto refused = methods::SurfaceModel::prepare({source, {}}, options(), empty, {}, report);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::resource);
    CHECK(empty.used() == 0);
    for (const auto phase : {core::Checkpoint::measurement, core::Checkpoint::solving}) {
        core::Budget budget{surface_budget};
        CheckpointStop const stop{phase, 0};
        auto cancelled = methods::SurfaceModel::prepare({source, {}}, options(), budget,
                                                        stop.cancellation(), report);
        REQUIRE(!cancelled);
        CHECK(cancelled.error().code == core::ErrorCode::cancelled);
        CHECK(budget.used() == 0);
    }
    core::Budget budget{surface_budget};
    auto model = methods::SurfaceModel::prepare({source, {}}, options(), budget, {}, report);
    REQUIRE(model);
    const CheckpointStop stop{core::Checkpoint::processing, 0};
    const image::RowRange origin;
    std::array<double, image::rgb_channels> pixel{paper, paper, paper};
    const auto cancelled = model->apply(origin, pixel, {}, report, stop.cancellation());
    REQUIRE(!cancelled);
    CHECK(pixel.front() == paper);
}
TEST_CASE("I01 automatic predicates each carry their independent decision", "[surface][auto]") {
    constexpr double bright = 0.8;
    constexpr double median = 0.7;
    constexpr double variation = 0.2;
    constexpr double paper_fraction = 0.8;
    constexpr double dark = 0.1;
    methods::IlluminationReport report;
    report.cells = 10;
    report.measured_cells = 10;
    report.measurements = methods::SurfaceMeasurements{
        .luminance_q90 = bright,
        .variation = variation,
        .paper_fraction = paper_fraction,
        .dark_fraction = dark,
    };
    report.measurements->background_q50 = median;
    REQUIRE(methods::surface_eligible(report));
    for (std::size_t i = 0; i < methods::surface_predicates; ++i) {
        auto candidate = report;
        switch (i) {
        case 0:
            candidate.measured_cells = 0;
            break;
        case 1:
            candidate.measurements->luminance_q90 = 0;
            break;
        case 2:
            candidate.measurements->background_q50 = 0;
            break;
        case 3:
            candidate.measurements->variation = 0;
            break;
        case 4:
            candidate.measurements->paper_fraction = 0;
            break;
        default:
            candidate.measurements->dark_fraction = 0;
            break;
        }
        CHECK(!methods::surface_eligible(candidate));
        REQUIRE(candidate.predicates.at(i).has_value());
        CHECK(candidate.predicates.at(i) == false);
    }
}

TEST_CASE("I01 exact required budget succeeds and one-byte-short refusal refunds",
          "[surface][resources]") {
    core::Budget source_budget{surface_budget};
    LinearFixture source{source_budget, page};
    source.fill(paper);
    const auto fits = [&](std::size_t bytes) {
        core::Budget budget{bytes};
        bool success = false;
        {
            methods::IlluminationReport report;
            auto result =
                methods::SurfaceModel::prepare({source, {}}, options(), budget, {}, report);
            success = result.has_value();
            if (!success) {
                CHECK(result.error().code == core::ErrorCode::resource);
            }
        }
        CHECK(budget.used() == 0);
        return success;
    };
    std::size_t lower = 0;
    std::size_t upper = surface_budget;
    REQUIRE(fits(upper));
    while (lower < upper) {
        const auto middle = lower + ((upper - lower) / 2);
        if (fits(middle)) {
            upper = middle;
        } else {
            lower = middle + 1;
        }
    }
    REQUIRE(lower > 0);
    CHECK(fits(lower));
    CHECK(!fits(lower - 1));
    CHECK(source.row(0).front() == paper);
}

TEST_CASE("I01 singleton and wholly protected lattice have defined measurements",
          "[surface][statistics]") {
    core::Budget budget{surface_budget};
    {
        LinearFixture singleton{budget, {.width = 1, .height = 1}};
        singleton.fill(paper);
        methods::IlluminationReport report;
        const auto model =
            methods::SurfaceModel::prepare({singleton, {}}, options(), budget, {}, report);
        REQUIRE(model);
        CHECK(model->background(0, 0).value() == paper);
        REQUIRE(report.measurements);
        if (report.measurements) {
            CHECK(report.measurements->count == 1);
        }
    }
    constexpr std::uint32_t width = 1025;
    LinearFixture source{budget, {.width = width, .height = 1}};
    source.fill(paper);
    auto mask = image::Plane<std::uint8_t>::allocate(budget, width, 1).value();
    for (std::uint32_t x = 0; x < width; ++x) {
        mask.view().row(0).subspan(x, 1).front() = static_cast<std::uint8_t>(x % 2 == 0);
    }
    auto params = options().parameters();
    params.cell = 32;
    methods::IlluminationReport report;
    const auto result = methods::SurfaceModel::prepare({source, mask.view().as_const()},
                                                       methods::Surface::create(params).value(),
                                                       budget, {}, report);
    REQUIRE(result);
    REQUIRE(report.measurements);
    if (report.measurements) {
        CHECK(report.measurements->fallback);
        CHECK(report.measurements->stride == 2);
        CHECK(report.measurements->count == width / 2);
    }
}
TEST_CASE("Solid dark content is left unmeasured and spanned from surrounding paper",
          "[surface][dark]") {
    constexpr image::Extent extent{.width = 48, .height = 32};
    constexpr std::uint32_t block_cells = 4;
    constexpr std::uint32_t dark_everywhere = 20; // 6 x 4 cells less the first column.
    constexpr double field_tolerance = 1e-6;
    core::Budget budget{surface_budget};
    LinearFixture source{budget, extent};
    paint_dark(source, false);
    methods::IlluminationReport report;
    const auto model = methods::SurfaceModel::prepare({source, {}}, options(), budget, {}, report);
    REQUIRE(model);
    REQUIRE(model->active());
    CHECK(report.dark_cells == block_cells);
    CHECK(report.measured_cells == report.cells - block_cells);
    CHECK(std::abs(report.background_reference.value_or(0) - dark_paper) < field_tolerance);
    CHECK(std::abs(model->background(cell, 2 * cell).value() - dark_paper) < field_tolerance);
    paint_dark(source, true);
    methods::IlluminationReport refused_report;
    const auto refused =
        methods::SurfaceModel::prepare({source, {}}, options(), budget, {}, refused_report);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::method_inapplicable);
    CHECK(refused_report.dark_cells == dark_everywhere);
}
} // namespace docenhance::tests
