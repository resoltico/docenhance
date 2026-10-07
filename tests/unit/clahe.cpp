// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "contrast.hpp"
#include "contrast_detail.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast.hpp"
#include "linear_fixture.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
namespace docenhance::tests {
namespace {
constexpr std::size_t limit = std::size_t{2} * 1024 * 1024;
methods::Contrast selection() {
    return methods::Clahe::create({.grid_columns = 2, .grid_rows = 2}).value();
}
void populate(LinearFixture& source) {
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        for (std::uint32_t x = 0; x < source.extent().width; ++x) {
            const double f = (x % 16 < 8) ? .25 : .75;
            std::ranges::fill(
                source.row(y).subspan(std::size_t{x} * image::rgb_channels, image::rgb_channels),
                image::srgb_decode(f).value());
        }
    }
}
} // namespace
TEST_CASE("CLAHE integer redistribution conserves mass and maps have anchored monotonic knots",
          "[contrast][clahe]") {
    core::Budget budget{limit};
    LinearFixture source{budget, {.width = 32, .height = 32}};
    for (std::uint32_t y = 0; y < source.extent().height; ++y) {
        for (std::uint32_t x = 0; x < source.extent().width; ++x) {
            std::ranges::fill(
                source.row(y).subspan(std::size_t{x} * image::rgb_channels, image::rgb_channels),
                x % 16 < 8 ? 0.0 : 1.0);
        }
    }
    const auto method = std::get<methods::Clahe>(selection());
    methods::ContrastReport report;
    report.eligible_samples = 1024;
    auto maps =
        methods::measure_clahe(source, {}, method, {budget, {}, report, image::RowUse::output});
    REQUIRE(maps);
    // Each tile has 256 samples: two endpoint bins clip to one, then the first 254
    // bins receive one. Independent integer cumulative counts are the oracle.
    for (std::uint32_t tile = 0; tile < 4; ++tile) {
        const auto knots = maps->knots.view().row(tile);
        CHECK(knots.front() == 0);
        CHECK(knots.back() == 1);
        CHECK(!maps->identity.at(tile));
        for (std::uint32_t k = 0; k <= methods::clahe_bins; ++k) {
            const auto mass = std::min(k, std::uint32_t{254}) + static_cast<std::uint32_t>(k > 0) +
                              static_cast<std::uint32_t>(k > 1023);
            CHECK(knots[k] == static_cast<double>(mass) / 256);
            if (k != 0) {
                CHECK(knots[k] >= knots[k - 1]);
            }
        }
    }
    CHECK(report.measured_samples == 1024);
}
TEST_CASE("CLAHE resource refusal and every preparation stop refund charged maps",
          "[contrast][clahe]") {
    core::Budget source_budget{limit};
    LinearFixture source{source_budget, {.width = 33, .height = 35}};
    populate(source);
    methods::ContrastReport report;
    core::Budget budget{limit};
    std::size_t peak = 0;
    {
        auto model = methods::ContrastModel::prepare(source, {}, selection(),
                                                     {budget, {}, report, image::RowUse::output});
        REQUIRE(model);
        CHECK(model->measured_source());
        peak = static_cast<std::size_t>(report.preparation_charge_peak);
        CHECK(budget.used() != 0);
    }
    CHECK(budget.used() == 0);
    {
        core::Budget exact{peak};
        auto model = methods::ContrastModel::prepare(source, {}, selection(),
                                                     {exact, {}, report, image::RowUse::output});
        REQUIRE(model);
        host::ContrastedSource output{{.source = source, .mask = {}}, &*model, report, {}};
        REQUIRE(host::assess_contrast(output, exact, {}, report));
        CHECK(methods::valid_contrast(report, selection()));
        const auto observations = report;
        std::array<double, 99> rgb{};
        REQUIRE(output.read({.row = 0, .first = 0}, rgb, image::RowUse::verification));
        CHECK(report == observations);
    }
    core::Budget short_budget{peak - 1};
    auto refused = methods::ContrastModel::prepare(
        source, {}, selection(), {short_budget, {}, report, image::RowUse::output});
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::resource);
    CHECK(short_budget.used() == 0);
    for (const auto phase : {core::Checkpoint::allocation, core::Checkpoint::measurement}) {
        std::size_t visits = 0;
        {
            const CheckpointStop observe{phase, std::numeric_limits<std::size_t>::max()};
            REQUIRE(methods::ContrastModel::prepare(
                source, {}, selection(),
                {budget, observe.cancellation(), report, image::RowUse::output}));
            visits = CheckpointStop::visits();
        }
        for (std::size_t allowed = 0; allowed < visits; ++allowed) {
            const CheckpointStop stop{phase, allowed};
            auto model = methods::ContrastModel::prepare(
                source, {}, selection(),
                {budget, stop.cancellation(), report, image::RowUse::output});
            REQUIRE(!model);
            CHECK(model.error().code == core::ErrorCode::cancelled);
            CHECK(budget.used() == 0);
            CHECK(methods::valid_contrast_observations(report));
        }
    }
}
TEST_CASE("CLAHE exact identities and strict tile applicability retain honest reports",
          "[contrast][clahe]") {
    core::Budget budget{limit};
    LinearFixture source{budget, {.width = 32, .height = 32}};
    source.fill(.4);
    methods::ContrastReport report;
    auto model = methods::ContrastModel::prepare(source, {}, selection(),
                                                 {budget, {}, report, image::RowUse::output});
    REQUIRE(model);
    CHECK(report.identity_tiles == 4);
    host::ContrastedSource output{{.source = source, .mask = {}}, &*model, report, {}};
    REQUIRE(host::assess_contrast(output, budget, {}, report));
    CHECK(report.changed_samples == 0);
    CHECK(methods::valid_contrast(report, selection()));
    std::array<double, 96> rgb{};
    REQUIRE(output.read({.row = 0, .first = 0}, rgb, image::RowUse::verification));
    CHECK(std::ranges::all_of(rgb, [](double value) { return value == .4; }));
    LinearFixture small{budget, {.width = 31, .height = 32}};
    small.fill(.4);
    auto refused = methods::ContrastModel::prepare(small, {}, selection(),
                                                   {budget, {}, report, image::RowUse::output});
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::argument);
    CHECK(!methods::Clahe::create({.grid_columns = 1}));
    CHECK(!methods::Clahe::create({.grid_rows = 33}));
    CHECK(!methods::Clahe::create({.clip = std::numeric_limits<double>::quiet_NaN()}));
}

TEST_CASE("CLAHE application stops preserve partial observations and frozen resident maps",
          "[contrast][clahe]") {
    core::Budget budget{limit};
    LinearFixture source{budget, {.width = 32, .height = 32}};
    populate(source);
    methods::ContrastReport initial;
    auto model = methods::ContrastModel::prepare(source, {}, selection(),
                                                 {budget, {}, initial, image::RowUse::output});
    REQUIRE(model);
    const auto resident = budget.used();
    std::size_t visits = 0;
    {
        auto report = initial;
        const CheckpointStop observe{core::Checkpoint::processing,
                                     std::numeric_limits<std::size_t>::max()};
        host::ContrastedSource output{
            {.source = source, .mask = {}}, &*model, report, observe.cancellation()};
        REQUIRE(host::assess_contrast(output, budget, observe.cancellation(), report));
        visits = CheckpointStop::visits();
    }
    for (std::size_t allowed = 0; allowed < visits; ++allowed) {
        auto report = initial;
        const CheckpointStop stop{core::Checkpoint::processing, allowed};
        host::ContrastedSource output{
            {.source = source, .mask = {}}, &*model, report, stop.cancellation()};
        auto result = host::assess_contrast(output, budget, stop.cancellation(), report);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(!report.complete);
        CHECK(methods::valid_contrast_observations(report));
        CHECK(budget.used() == resident);
    }
}
TEST_CASE("CLAHE identity cutoffs are strict for eligible count and true sample range",
          "[contrast][clahe]") {
    core::Budget budget{limit};
    LinearFixture source{budget, {.width = 32, .height = 32}};
    auto mask = image::Plane<std::uint8_t>::allocate(budget, 32, 32).value();
    std::ranges::fill(mask.view().storage(), 1);
    for (const auto count : {15U, 16U}) {
        for (const double range : {1.0 / 8192, 1.0 / 4096}) {
            source.fill(0);
            std::ranges::fill(mask.view().storage(), 1);
            for (std::uint32_t x = 0; x < count; ++x) {
                mask.view().row(0).subspan(x, 1).front() = 0;
            }
            std::ranges::fill(source.row(0).first(image::rgb_channels),
                              image::srgb_decode(range).value());
            methods::ContrastReport report;
            auto model =
                methods::ContrastModel::prepare(source, mask.view().as_const(), selection(),
                                                {budget, {}, report, image::RowUse::output});
            REQUIRE(model);
            CHECK(report.measured_samples == count);
            CHECK(report.identity_tiles == (count < 16 || range < 1.0 / 4096 ? 4U : 3U));
        }
    }
}
} // namespace docenhance::tests
