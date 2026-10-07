// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/morphology.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/gaussian.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "linear_fixture.hpp"
#include "morphology_reference.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>
namespace docenhance::tests {
namespace {
constexpr std::size_t morphology_budget = std::size_t{2} * 1024 * 1024;
methods::Morphology options(unsigned radius = 1) {
    return methods::Morphology::create({.radius = radius}).value();
}
methods::MorphologyMeasurements observations(const methods::IlluminationReport& report) {
    if (report.morphology) {
        return *report.morphology;
    }
    FAIL("Expected I02 field observations");
    return {};
}
void compare_field(const methods::MorphologyModel& model, image::Extent extent,
                   std::span<const double> reference) {
    for (std::size_t i = 0; i < reference.size(); ++i) {
        const auto b = model.background(static_cast<unsigned>(i % extent.width),
                                        static_cast<unsigned>(i / extent.width));
        REQUIRE(b);
        REQUIRE(std::isfinite(*b));
        CHECK(std::abs(*b - reference[i]) < 1e-12);
    }
}
void independent_case(image::Extent extent, unsigned radius) {
    core::Budget budget{morphology_budget};
    LinearFixture source{budget, extent};
    std::vector<double> samples;
    for (unsigned y = 0; y < extent.height; ++y) {
        for (unsigned x = 0; x < extent.width; ++x) {
            constexpr unsigned range = 101;
            const auto value = static_cast<double>(((19 * x) + (37 * y)) % range) / (range - 1);
            samples.push_back(value);
            std::ranges::fill(
                source.row(y).subspan(std::size_t{x} * image::rgb_channels, image::rgb_channels),
                value);
        }
    }
    const auto reference =
        morphology_reference(samples, {.width = extent.width, .height = extent.height}, radius);
    const auto entering_charge = budget.used();
    {
        methods::IlluminationReport report;
        auto prepared =
            methods::MorphologyModel::prepare({source, {}}, options(radius), budget, {}, report);
        REQUIRE(prepared);
        REQUIRE(prepared->active());
        compare_field(*prepared, extent, reference);
    }
    CHECK(budget.used() == entering_charge);
}
} // namespace
TEST_CASE("I02 float64 closing and smoothing agree with independent square and Gaussian references",
          "[morphology]") {
    for (const auto extent : std::to_array<image::Extent>({
             {.width = 1, .height = 1},
             {.width = 1, .height = 9},
             {.width = 7, .height = 1},
             {.width = 3, .height = 5},
             {.width = 7, .height = 9},
         })) {
        for (const auto radius : {1U, 3U, 16U}) {
            independent_case(extent, radius);
        }
    }
    independent_case({.width = 1, .height = 1}, methods::Morphology::max_radius);
}
TEST_CASE("I02 admission resolves exact radius and rejects every invalid parameter",
          "[morphology]") {
    CHECK(methods::morphology_radius({.width = 375, .height = 800},
                                     methods::Morphology::create().value()) == 8);
    CHECK(methods::morphology_radius({.width = 425, .height = 800},
                                     methods::Morphology::create().value()) == 9);
    CHECK(methods::morphology_radius({.width = 10000, .height = 10000},
                                     methods::Morphology::create().value()) == 128);
    CHECK(methods::morphology_radius({.width = 1, .height = 1}, options(256)) == 256);
    for (const auto r : {0U, 257U, std::numeric_limits<unsigned>::max()}) {
        CHECK(!methods::Morphology::create({.radius = r}));
    }
    for (const auto v : {-1.0, 1.01, std::numeric_limits<double>::quiet_NaN()}) {
        CHECK(!methods::Morphology::create({.strength = v}));
    }
    CHECK(!methods::Morphology::create({.max_gain = 4.01}));
    CHECK(!methods::Morphology::create({.target = 0.09}));
}
TEST_CASE("I02 protected analysis fill and output bypass preserve the source", "[morphology]") {
    core::Budget budget{morphology_budget};
    constexpr image::Extent extent{.width = 9, .height = 5};
    LinearFixture source{budget, extent};
    source.fill(0.4);
    auto mask = image::Plane<std::uint8_t>::allocate(budget, extent.width, extent.height).value();
    std::ranges::fill(mask.view().storage(), std::uint8_t{0});
    mask.view().row(2).subspan(4, 1).front() = 1;
    std::ranges::fill(source.row(2).subspan(4 * image::rgb_channels, image::rgb_channels), 0.0);
    methods::IlluminationReport report;
    auto model = methods::MorphologyModel::prepare({source, mask.view().as_const()}, options(),
                                                   budget, {}, report);
    REQUIRE(model);
    REQUIRE(report.morphology);
    CHECK(observations(report).analysis_fill == 0.4);
    const auto center = model->background(4, 2);
    REQUIRE(center);
    CHECK(*center == 0.4);
    std::array<double, 3> pixel{};
    REQUIRE(model->apply({.row = 2, .first = 4}, pixel, mask.view().as_const(), report, {}));
    CHECK(pixel == std::array<double, 3>{});
    CHECK(report.evaluated_samples == 0);
    CHECK(source.row(2).subspan(4 * image::rgb_channels, 1).front() == 0);
}
TEST_CASE("I02 no-ops and all-black statistics do not manufacture changes", "[morphology]") {
    core::Budget budget{morphology_budget};
    constexpr image::Extent extent{.width = 8, .height = 4};
    LinearFixture source{budget, extent};
    source.fill(0);
    methods::IlluminationReport report;
    auto model = methods::MorphologyModel::prepare({source, {}}, options(), budget, {}, report);
    REQUIRE(model);
    REQUIRE(report.morphology);
    CHECK(observations(report).target == 0);
    for (unsigned y = 0; y < extent.height; ++y) {
        REQUIRE(model->apply({.row = y, .first = 0}, source.row(y), {}, report, {}));
    }
    report.complete = true;
    CHECK(methods::valid_illumination(report, extent, false));
    const auto entering_charge = budget.used();
    const auto zero = methods::Morphology::create({.strength = 0, .radius = 256}).value();
    auto inactive = methods::MorphologyModel::prepare({source, {}}, zero, budget, {}, report);
    REQUIRE(inactive);
    CHECK(!inactive->active());
    CHECK(report.complete);
    CHECK(observations(report).field_bytes == 0);
    CHECK(budget.used() == entering_charge);
}
TEST_CASE("I02 phase budget boundaries and every preparation checkpoint refund ownership",
          "[morphology]") {
    core::Budget input_budget{morphology_budget};
    LinearFixture source{input_budget, {.width = 8, .height = 4}};
    source.fill(0.4);
    methods::IlluminationReport report;
    core::Budget measured{morphology_budget};
    {
        auto model =
            methods::MorphologyModel::prepare({source, {}}, options(), measured, {}, report);
        REQUIRE(model);
    }
    CHECK(measured.used() == 0);
    const auto peak = static_cast<std::size_t>(observations(report).preparation_charge_peak);
    core::Budget exact{peak};
    {
        auto model = methods::MorphologyModel::prepare({source, {}}, options(), exact, {}, report);
        REQUIRE(model);
    }
    CHECK(exact.used() == 0);
    core::Budget refused{peak - 1};
    CHECK(!methods::MorphologyModel::prepare({source, {}}, options(), refused, {}, report));
    CHECK(refused.used() == 0);
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::measurement,
             core::Checkpoint::processing,
         }) {
        std::size_t visits = 0;
        {
            const CheckpointStop observe{phase, std::numeric_limits<std::size_t>::max()};
            auto model = methods::MorphologyModel::prepare({source, {}}, options(), measured,
                                                           observe.cancellation(), report);
            REQUIRE(model);
            visits = CheckpointStop::visits();
        }
        REQUIRE(visits > 0);
        for (std::size_t allowed = 0; allowed < visits; ++allowed) {
            const CheckpointStop stop{phase, allowed};
            auto model = methods::MorphologyModel::prepare({source, {}}, options(), measured,
                                                           stop.cancellation(), report);
            REQUIRE(!model);
            CHECK(model.error().code == core::ErrorCode::cancelled);
            CHECK(measured.used() == 0);
        }
    }
}
TEST_CASE("I02 protects its kernel storage and finite input contracts", "[morphology]") {
    core::Budget budget{morphology_budget};
    auto input = image::Plane<double>::allocate(budget, 3, 2).value();
    auto output = image::Plane<double>::allocate(budget, 3, 2).value();
    std::ranges::fill(input.view().storage(), 0.4);
    std::array<std::uint64_t, 3> queue{};
    CHECK(!methods::extrema_pass(input.view().as_const(), input.view(), queue, {.radius = 1}));
    CHECK(!methods::extrema_pass(input.view().as_const(), output.view(), queue, {.radius = 2}));
    const std::array<double, 3> weights{0.5, 1, 0.5};
    CHECK(!image::gaussian_pass(input.view().as_const(), input.view(),
                                {.weights = weights, .normalization = 2}));
    CHECK(!image::gaussian_pass(input.view().as_const(), output.view(),
                                {.weights = weights, .normalization = 1}));
    std::ranges::fill(output.view().row(0), 1.0);
    CHECK(!image::gaussian_pass(input.view().as_const(), output.view(),
                                {.weights = output.view().row(0), .normalization = 3}));
    REQUIRE(image::gaussian_pass(input.view().as_const(), output.view(),
                                 {.weights = weights, .normalization = 2}));
    CHECK(output.view().row(0).front() == 0.4);
    input.view().row(0).front() = std::numeric_limits<double>::quiet_NaN();
    const auto extrema =
        methods::extrema_pass(input.view().as_const(), output.view(), queue, {.radius = 1});
    REQUIRE(!extrema);
    CHECK(extrema.error().code == core::ErrorCode::numerical);
    const auto gaussian = image::gaussian_pass(input.view().as_const(), output.view(),
                                               {.weights = weights, .normalization = 2});
    REQUIRE(!gaussian);
    CHECK(gaussian.error().code == core::ErrorCode::numerical);
}
TEST_CASE("I02 broad dark content remains in its field and moved models lose ownership",
          "[morphology]") {
    core::Budget budget{morphology_budget};
    LinearFixture source{budget, {.width = 17, .height = 17}};
    source.fill(0.9);
    for (unsigned y = 4; y <= 12; ++y) {
        std::ranges::fill(source.row(y).subspan(4 * image::rgb_channels, 9 * image::rgb_channels),
                          0.1);
    }
    methods::IlluminationReport report;
    auto model = methods::MorphologyModel::prepare({source, {}}, options(), budget, {}, report);
    REQUIRE(model);
    CHECK(model->background(8, 8).value() < 0.11);
    const auto moved = std::move(*model);
    CHECK(!model->active());
    CHECK(!model->background(0, 0));
    CHECK(moved.active());
    CHECK(moved.background(0, 0).value() > 0.89);
}
TEST_CASE("I02 fallback samples only the first eligible pixel and refuses inadequate eligibility",
          "[morphology]") {
    core::Budget budget{morphology_budget};
    LinearFixture source{budget, {.width = 1025, .height = 1}};
    source.fill(0.3);
    auto mask = image::Plane<std::uint8_t>::allocate(budget, 1025, 1).value();
    std::ranges::fill(mask.view().storage(), std::uint8_t{1});
    for (unsigned x = 1; x < 34; x += 2) {
        mask.view().row(0).subspan(x, 1).front() = 0;
    }
    std::ranges::fill(source.row(0).subspan(image::rgb_channels, image::rgb_channels), 0.2);
    methods::IlluminationReport report;
    auto model = methods::MorphologyModel::prepare({source, mask.view().as_const()}, options(),
                                                   budget, {}, report);
    REQUIRE(model);
    REQUIRE(report.morphology);
    CHECK(observations(report).fallback);
    CHECK(observations(report).count == 1);
    CHECK(observations(report).analysis_fill == 0.2);
    mask.view().row(0).subspan(1, 1).front() = 1;
    mask.view().row(0).subspan(3, 1).front() = 1;
    const auto refused = methods::MorphologyModel::prepare({source, mask.view().as_const()},
                                                           options(), budget, {}, report);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::method_inapplicable);
}
} // namespace docenhance::tests
