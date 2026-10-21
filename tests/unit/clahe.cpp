// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/clahe.hpp"

#include "cancellation_probe.hpp"
#include "clahe_fixture.hpp"
#include "contrast_detail.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "gamma_fixture.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
namespace docenhance::tests {
namespace {
constexpr std::size_t limit = std::size_t{2} * 1024 * 1024;
methods::Clahe selection() {
    const auto method = methods::Clahe::create({.grid_columns = 2, .grid_rows = 2});
    REQUIRE(method);
    return *method;
}
void populate(ClaheFixture& source) {
    for (std::uint32_t y = 0; y < source.luminance.height(); ++y) {
        for (std::uint32_t x = 0; x < source.luminance.width(); ++x) {
            const double f = x % 16 < 8 ? .25 : .75;
            source.luminance.view().row(y).subspan(x, 1).front() = image::srgb_decode(f);
        }
    }
}
void check_clahe_knot(std::span<const double> knots, std::uint32_t k) {
    const auto mass = std::min(k, std::uint32_t{254}) + static_cast<std::uint32_t>(k > 0) +
                      static_cast<std::uint32_t>(k > 1023);
    CHECK(knots[k] == static_cast<double>(mass) / 256);
    if (k != 0) {
        CHECK(knots[k] >= knots[k - 1]);
    }
}
void check_clahe_tile(const methods::ClaheMaps& maps, std::uint32_t tile) {
    const auto knots = maps.knots.view().row(tile);
    CHECK(knots.front() == 0);
    CHECK(knots.back() == 1);
    CHECK(!maps.identity.at(tile));
    for (std::uint32_t k = 0; k <= methods::clahe_bins; ++k) {
        check_clahe_knot(knots, k);
    }
}
void check_clahe_knots(const methods::ClaheMaps& maps) {
    for (std::uint32_t tile = 0; tile < 4; ++tile) {
        check_clahe_tile(maps, tile);
    }
}
void fit_and_check_clahe(ClaheFixture& source, core::Budget& budget,
                         const exec::Scheduler& scheduler,
                         const std::vector<std::uint64_t>& before) {
    const methods::Working working{source.luminance.view(), {}, budget, scheduler};
    const auto counts = methods::eligibility(working);
    REQUIRE(counts);
    REQUIRE(counts->eligible == 1024);
    const auto maps = methods::fit_clahe(selection(), working, counts->eligible);
    REQUIRE(maps);
    check_clahe_knots(*maps);
    CHECK(budget.used() != 0);
    CHECK(gamma_bits(source.luminance.view().storage()) == before);
}
} // namespace
TEST_CASE("CLAHE integer redistribution conserves mass and maps have anchored monotonic knots",
          "[contrast][clahe]") {
    ClaheFixture source{32, 32, 0};
    for (std::uint32_t y = 0; y < 32; ++y) {
        for (std::uint32_t x = 0; x < 32; ++x) {
            source.luminance.view().row(y).subspan(x, 1).front() = x % 16 < 8 ? 0.0 : 1.0;
        }
    }
    const auto before = gamma_bits(source.luminance.view().storage());
    core::Budget budget{limit};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    {
        fit_and_check_clahe(source, budget, scheduler, before);
    }
    CHECK(budget.used() == 0);
}
TEST_CASE("CLAHE resource refusal and every fitting stop refund charged maps",
          "[contrast][clahe]") {
    ClaheFixture source{33, 35, 0};
    populate(source);
    const auto method = selection();
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    core::Budget budget{limit};
    REQUIRE(methods::apply(method, {source.luminance.view(), {}, budget, scheduler}));
    const auto peak = budget.peak();
    REQUIRE(peak > 0);
    CHECK(budget.used() == 0);
    for (const bool enough : {true, false}) {
        populate(source);
        const auto before = gamma_bits(source.luminance.view().storage());
        core::Budget bounded{peak - static_cast<std::size_t>(!enough)};
        const auto result =
            methods::apply(method, {source.luminance.view(), {}, bounded, scheduler});
        if (enough) {
            REQUIRE(result);
            CHECK(methods::validate(*result, method.parameters()));
            CHECK(result->counts.evaluated == std::uint64_t{33} * 35);
        } else {
            REQUIRE(!result);
            CHECK(result.error().code == core::ErrorCode::resource);
            CHECK(gamma_bits(source.luminance.view().storage()) == before);
        }
        CHECK(bounded.used() == 0);
    }
    for (const auto phase : {core::Checkpoint::allocation, core::Checkpoint::measurement}) {
        populate(source);
        const CheckpointStop observe{phase, std::numeric_limits<std::size_t>::max()};
        const exec::Scheduler observed{*concurrency, observe.cancellation()};
        REQUIRE(methods::apply(method, {source.luminance.view(), {}, budget, observed}));
        const auto visits = CheckpointStop::visits();
        REQUIRE(visits > 0);
        for (std::size_t allowed = 0; allowed < visits; ++allowed) {
            populate(source);
            const auto before = gamma_bits(source.luminance.view().storage());
            const CheckpointStop stop{phase, allowed};
            const exec::Scheduler stopped{*concurrency, stop.cancellation()};
            const auto result =
                methods::apply(method, {source.luminance.view(), {}, budget, stopped});
            REQUIRE(!result);
            CHECK(result.error().code == core::ErrorCode::cancelled);
            CHECK(gamma_bits(source.luminance.view().storage()) == before);
            CHECK(budget.used() == 0);
        }
    }
}
TEST_CASE("CLAHE exact identities and strict tile applicability retain honest Facts",
          "[contrast][clahe]") {
    ClaheFixture source{32, 32, 0};
    for (std::uint32_t y = 0; y < 32; ++y) {
        std::ranges::fill(source.luminance.view().row(y), .4);
    }
    const auto before = gamma_bits(source.luminance.view().storage());
    const auto method = selection();
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    core::Budget budget{limit};
    const auto facts = methods::apply(method, {source.luminance.view(), {}, budget, scheduler});
    REQUIRE(facts);
    CHECK(facts->identity_tiles == 4);
    CHECK(facts->counts.evaluated == 1024);
    CHECK(facts->counts.changed == 0);
    CHECK(methods::validate(*facts, method.parameters()));
    CHECK(gamma_bits(source.luminance.view().storage()) == before);
    ClaheFixture small{31, 32, 0};
    const auto refused = methods::apply(method, {small.luminance.view(), {}, budget, scheduler});
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::argument);
    CHECK(budget.used() == 0);
    CHECK(!methods::Clahe::create({.grid_columns = 1}));
    CHECK(!methods::Clahe::create({.grid_rows = 33}));
    CHECK(!methods::Clahe::create({.clip = std::numeric_limits<double>::quiet_NaN()}));
}
TEST_CASE("CLAHE application stops preserve attained rows and return no completed Facts",
          "[contrast][clahe]") {
    ClaheFixture source{32, 32, 0};
    populate(source);
    const auto before = gamma_bits(source.luminance.view().storage());
    const auto method = selection();
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const CheckpointStop observe{core::Checkpoint::processing,
                                 std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler scheduler{*concurrency, observe.cancellation()};
    core::Budget budget{limit};
    const auto healthy = methods::apply(method, {source.luminance.view(), {}, budget, scheduler});
    REQUIRE(healthy);
    REQUIRE(healthy->counts.changed != 0);
    REQUIRE(CheckpointStop::visits() == 32);
    const auto completed = gamma_bits(source.luminance.view().storage());
    for (std::size_t allowed = 0; allowed < 32; ++allowed) {
        populate(source);
        const CheckpointStop stop{core::Checkpoint::processing, allowed};
        const exec::Scheduler stopped{*concurrency, stop.cancellation()};
        const auto result = methods::apply(method, {source.luminance.view(), {}, budget, stopped});
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(budget.used() == 0);
        const auto after = gamma_bits(source.luminance.view().storage());
        const auto pitch = source.luminance.view().row_pitch();
        for (std::size_t i = 0; i < after.size(); ++i) {
            const bool stored = i / pitch < allowed && i % pitch < 32;
            CHECK(after.at(i) == (stored ? completed.at(i) : before.at(i)));
        }
    }
}
TEST_CASE("CLAHE identity cutoffs are strict for eligible count and true sample range",
          "[contrast][clahe]") {
    ClaheFixture source{32, 32, 0};
    core::Budget budget{limit};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    for (const auto count : {15U, 16U}) {
        for (const double range : {1.0 / 8192, 1.0 / 4096}) {
            for (std::uint32_t y = 0; y < 32; ++y) {
                std::ranges::fill(source.luminance.view().row(y), 0);
                std::ranges::fill(source.protection.view().row(y), 1);
            }
            std::ranges::fill(source.protection.view().row(0).first(count), 0);
            source.luminance.view().row(0).front() = image::srgb_decode(range);
            const auto facts = methods::apply(
                selection(),
                {source.luminance.view(), source.protection.view().as_const(), budget, scheduler});
            REQUIRE(facts);
            CHECK(facts->counts.eligible == count);
            CHECK(facts->counts.evaluated == count);
            CHECK(facts->identity_tiles == (count < 16 || range < 1.0 / 4096 ? 4U : 3U));
            CHECK(budget.used() == 0);
        }
    }
}
} // namespace docenhance::tests
