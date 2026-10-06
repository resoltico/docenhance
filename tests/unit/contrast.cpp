// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/contrast.hpp"

#include "cancellation_probe.hpp"
#include "contrast.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "linear_fixture.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
namespace docenhance::tests {
namespace {
constexpr std::size_t budget_limit = std::size_t{2} * 1024 * 1024;
}
TEST_CASE("Interruptible percentile sorting agrees with independent ordering and endpoints",
          "[contrast]") {
    for (std::size_t n = 1; n <= 257; ++n) {
        std::vector<double> input(n);
        for (std::size_t i = 0; i < n; ++i) {
            input[i] = static_cast<double>((i * 37) % 19) / 19;
        }
        auto expected = input;
        std::ranges::sort(expected);
        REQUIRE(image::sort_samples(input));
        CHECK(input == expected);
        for (const double p : {0.0, 0.005, 0.5, 0.995, 1.0}) {
            const auto rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(n)));
            const auto index = rank == 0 ? 0 : rank - 1;
            CHECK(image::nearest_rank_index(n, p).value() == index);
            auto scratch = expected;
            CHECK(image::nearest_rank(scratch, p).value() == expected[index]);
        }
    }
    std::array<double, 4> invalid{.7, .1, std::numeric_limits<double>::quiet_NaN(), .4};
    CHECK(!image::sort_samples(invalid));
    CHECK(invalid.front() == .7);
    CHECK(invalid[1] == .1);
    CHECK(!image::nearest_rank_index(0, .5));
    CHECK(image::nearest_rank_index(std::numeric_limits<std::size_t>::max(), 1).value() ==
          std::numeric_limits<std::size_t>::max() - 1);
}
TEST_CASE(
    "Contrast maps honor power convention, endpoints, strict range and exact transport identity",
    "[contrast]") {
    const auto gamma = methods::Gamma::create({.gamma = 2}).value();
    for (unsigned i = 0; i <= 1000; ++i) {
        const double f = static_cast<double>(i) / 1000;
        CHECK(std::abs(methods::gamma_candidate(f, gamma).value() - (f * f)) < 1e-12);
    }
    CHECK(methods::gamma_candidate(0, gamma).value() == 0);
    CHECK(methods::gamma_candidate(1, gamma).value() == 1);
    CHECK(methods::levels_candidate(.2, {.low = .2, .high = .8}).value() == 0);
    CHECK(methods::levels_candidate(.8, {.low = .2, .high = .8}).value() == 1);
    CHECK(!methods::levels_candidate(.5, {.low = .5, .high = .5000005}));
    CHECK(methods::levels_candidate(0, {.low = 0, .high = methods::levels_minimum_range}).value() ==
          0);
    const image::Rgb rgb{.1, .3, .7};
    CHECK(image::blend_perceptual(rgb, .5, .5, 1).value() == rgb);
    CHECK(image::blend_perceptual(rgb, .5, .7, 0).value() == rgb);
    CHECK(!methods::Levels::create({.low = -1}));
    CHECK(!methods::Levels::create({.high = 101}));
    CHECK(!methods::Gamma::create({.gamma = .1}));
    CHECK(!methods::Gamma::create({.gamma = 5}));
}
TEST_CASE(
    "Levels measures all eligible samples and frozen mappings preserve protection and observations",
    "[contrast]") {
    core::Budget budget{budget_limit};
    LinearFixture input{budget, {.width = 8, .height = 1}};
    const std::array<double, 8> f{0, .1, .2, .3, .4, .5, .8, 1};
    for (std::size_t i = 0; i < f.size(); ++i) {
        const auto y = image::srgb_decode(f.at(i)).value();
        std::ranges::fill(input.row(0).subspan(i * image::rgb_channels, image::rgb_channels), y);
    }
    auto mask = image::Plane<std::uint8_t>::allocate(budget, 8, 1).value();
    std::ranges::fill(mask.view().row(0), 0);
    mask.view().row(0).front() = 1;
    mask.view().row(0).back() = 1;
    const methods::Contrast selected = methods::Levels::create({.low = 0, .high = 100}).value();
    methods::ContrastReport report;
    auto model = methods::ContrastModel::prepare(input, mask.view().as_const(), selected,
                                                 {budget, {}, report, image::RowUse::output});
    REQUIRE(model);
    REQUIRE(model->active());
    REQUIRE(report.levels);
    CHECK(report.measured_samples == 6);
    if (report.levels) {
        CHECK(std::abs(report.levels->low - .1) < 1e-12);
        CHECK(std::abs(report.levels->high - .8) < 1e-12);
    }
    host::ContrastedSource source{
        {.source = input, .mask = mask.view().as_const()}, &*model, report, {}};
    REQUIRE(host::assess_contrast(source, budget, {}, report));
    CHECK(methods::valid_contrast(report, selected));
    CHECK(report.evaluated_samples == 6);
    const auto observed = report;
    std::array<double, 24> rgb{};
    REQUIRE(source.read({.row = 0, .first = 0}, rgb, image::RowUse::verification));
    CHECK(report == observed);
    CHECK(rgb.front() == 0);
    CHECK(rgb.back() == 1);
    CHECK(report.clipped_low_samples == 0);
    CHECK(report.clipped_high_samples == 0);
}
TEST_CASE("Contrast identities and flat eligible samples retain exact output without invented work",
          "[contrast]") {
    core::Budget budget{budget_limit};
    LinearFixture input{budget, {.width = 8, .height = 4}};
    input.fill(.4);
    for (const methods::Contrast& method : std::array<methods::Contrast, 3>{
             methods::Gamma::create({.gamma = 1}).value(),
             methods::Levels::create({.blend = 0}).value(),
             methods::Levels::create().value(),
         }) {
        methods::ContrastReport r;
        auto model = methods::ContrastModel::prepare(input, {}, method,
                                                     {budget, {}, r, image::RowUse::output});
        REQUIRE(model);
        CHECK(!model->active());
        CHECK(methods::valid_contrast(r, method));
        CHECK(r.changed_samples == 0);
        CHECK(r.evaluated_samples == 0);
    }
}
TEST_CASE("Contrast budgets and every bounded cancellation checkpoint preserve owned storage",
          "[contrast]") {
    core::Budget source_budget{budget_limit};
    LinearFixture input{source_budget, {.width = 16, .height = 8}};
    for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::size_t i = 0; i < input.row(y).size(); ++i) {
            input.row(y)[i] = .1 + (static_cast<double>(i) / 100);
        }
    }
    const methods::Contrast method = methods::Levels::create().value();
    methods::ContrastReport r;
    core::Budget measured{budget_limit};
    {
        REQUIRE(methods::ContrastModel::prepare(input, {}, method,
                                                {measured, {}, r, image::RowUse::output}));
    }
    const auto peak = static_cast<std::size_t>(r.preparation_charge_peak);
    CHECK(measured.used() == 0);
    core::Budget exact{peak};
    REQUIRE(
        methods::ContrastModel::prepare(input, {}, method, {exact, {}, r, image::RowUse::output}));
    CHECK(exact.used() == 0);
    core::Budget refused{peak - 1};
    auto failure =
        methods::ContrastModel::prepare(input, {}, method, {refused, {}, r, image::RowUse::output});
    REQUIRE(!failure);
    CHECK(failure.error().code == core::ErrorCode::resource);
    CHECK(refused.used() == 0);
    for (const auto phase : {core::Checkpoint::allocation, core::Checkpoint::measurement}) {
        std::size_t visits = 0;
        {
            const CheckpointStop observe{phase, std::numeric_limits<std::size_t>::max()};
            REQUIRE(methods::ContrastModel::prepare(
                input, {}, method, {measured, observe.cancellation(), r, image::RowUse::output}));
            visits = CheckpointStop::visits();
        }
        for (std::size_t allowed = 0; allowed < visits; ++allowed) {
            const CheckpointStop stop{phase, allowed};
            auto model = methods::ContrastModel::prepare(
                input, {}, method, {measured, stop.cancellation(), r, image::RowUse::output});
            REQUIRE(!model);
            CHECK(model.error().code == core::ErrorCode::cancelled);
            CHECK(measured.used() == 0);
            CHECK(methods::valid_contrast_observations(r));
        }
    }
}
TEST_CASE("Contrast cancellation during application retains truthful partial observations",
          "[contrast]") {
    core::Budget budget{budget_limit};
    LinearFixture input{budget, {.width = 512, .height = 2}};
    input.fill(.4);
    const methods::Contrast method = methods::Gamma::create({.gamma = 2}).value();
    methods::ContrastReport initial;
    auto model = methods::ContrastModel::prepare(input, {}, method,
                                                 {budget, {}, initial, image::RowUse::output});
    REQUIRE(model);
    const auto resident = budget.used();
    std::size_t visits = 0;
    {
        auto report = initial;
        const CheckpointStop observe{core::Checkpoint::processing,
                                     std::numeric_limits<std::size_t>::max()};
        host::ContrastedSource source{{.source = input, .mask = {}, .prepared = false},
                                      &*model,
                                      report,
                                      observe.cancellation()};
        REQUIRE(host::assess_contrast(source, budget, observe.cancellation(), report));
        visits = CheckpointStop::visits();
        CHECK(methods::valid_contrast(report, method));
    }
    REQUIRE(visits > 2);
    for (std::size_t allowed = 0; allowed < visits; ++allowed) {
        auto report = initial;
        const CheckpointStop stop{core::Checkpoint::processing, allowed};
        host::ContrastedSource source{
            {.source = input, .mask = {}, .prepared = false}, &*model, report, stop.cancellation()};
        auto result = host::assess_contrast(source, budget, stop.cancellation(), report);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(!report.complete);
        CHECK(methods::valid_contrast_observations(report));
        CHECK(budget.used() == resident);
    }
}
} // namespace docenhance::tests
