// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/contrast.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/gamma.hpp"
#include "docenhance/methods/levels.hpp"
#include "gamma_fixture.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
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
            const auto rank_index = image::nearest_rank_index(n, p);
            REQUIRE(rank_index);
            CHECK(*rank_index == index);
            auto scratch = expected;
            const auto selected = image::nearest_rank(scratch, p);
            REQUIRE(selected);
            CHECK(*selected == expected[index]);
        }
    }
    std::array<double, 4> invalid{.7, .1, std::numeric_limits<double>::quiet_NaN(), .4};
    CHECK(!image::sort_samples(invalid));
    CHECK(invalid.front() == .7);
    CHECK(invalid[1] == .1);
    CHECK(!image::nearest_rank_index(0, .5));
    const auto final_index = image::nearest_rank_index(std::numeric_limits<std::size_t>::max(), 1);
    REQUIRE(final_index);
    CHECK(*final_index == std::numeric_limits<std::size_t>::max() - 1);
}
TEST_CASE(
    "Contrast maps honor power convention, endpoints, strict range and exact transport identity",
    "[contrast]") {
    const auto gamma = methods::Gamma::create({.gamma = 2});
    REQUIRE(gamma);
    for (unsigned i = 0; i <= 1000; ++i) {
        const double f = static_cast<double>(i) / 1000;
        const double candidate = methods::gamma_candidate(f, *gamma);
        CHECK((std::isfinite(candidate) && candidate >= 0 && candidate <= 1));
        CHECK(std::abs(candidate - (f * f)) < 1e-12);
    }
    CHECK(methods::gamma_candidate(0, *gamma) == 0);
    CHECK(methods::gamma_candidate(1, *gamma) == 1);
    CHECK(methods::levels_candidate(.2, {.low = .2, .high = .8}) == 0);
    CHECK(methods::levels_candidate(.8, {.low = .2, .high = .8}) == 1);
    CHECK(methods::levels_candidate(0, {.low = 0, .high = methods::levels_minimum_range}) == 0);
    const image::Rgb rgb{.1, .3, .7};
    CHECK(image::transport_luminance(rgb, image::luminance(rgb)) == rgb);
    CHECK(!methods::Levels::create({.low = -1}));
    CHECK(!methods::Levels::create({.high = 101}));
    CHECK(!methods::Gamma::create({.gamma = .1}));
    CHECK(!methods::Gamma::create({.gamma = 5}));
}
TEST_CASE("Levels eagerly measures every eligible sample and preserves protection", "[contrast]") {
    GammaFixture input{8, 1};
    const std::array<double, 8> f{0, .1, .2, .3, .4, .5, .8, 1};
    for (std::size_t i = 0; i < f.size(); ++i) {
        input.luminance.view().row(0).subspan(i, 1).front() = image::srgb_decode(f.at(i));
    }
    std::ranges::fill(input.protection.view().row(0), 0);
    input.protection.view().row(0).front() = 1;
    input.protection.view().row(0).back() = 1;
    const auto method = methods::Levels::create({.low = 0, .high = 100});
    REQUIRE(method);
    core::Budget budget{budget_limit};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    const auto facts = methods::apply(
        *method, {input.luminance.view(), input.protection.view().as_const(), budget, scheduler});
    REQUIRE(facts);
    REQUIRE(facts->levels.has_value());
    if (!facts->levels.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    CHECK(std::abs(facts->levels.value().low - .1) < 1e-12);
    CHECK(std::abs(facts->levels.value().high - .8) < 1e-12);
    CHECK(facts->counts.eligible == 6);
    CHECK(facts->counts.protected_samples == 2);
    CHECK(facts->counts.evaluated == 6);
    CHECK(facts->clipped_low_samples == 0);
    CHECK(facts->clipped_high_samples == 0);
    CHECK(methods::validate(*facts, method->parameters()));
    for (std::size_t i = 0; i < f.size(); ++i) {
        const auto actual = input.luminance.view().row(0).subspan(i, 1).front();
        const auto expected = i == 0 || i == 7 ? f.at(i) : (f.at(i) - .1) / .7;
        CHECK(std::abs(image::srgb_encode(actual) - expected) < 1e-12);
    }
    CHECK(input.luminance.view().row(0).front() == 0);
    CHECK(input.luminance.view().row(0).back() == 1);
    CHECK(budget.used() == 0);
}
TEST_CASE("Contrast identities and flat eligible samples retain exact output without invented work",
          "[contrast]") {
    GammaFixture input{8, 4};
    for (std::uint32_t y = 0; y < 4; ++y) {
        std::ranges::fill(input.luminance.view().row(y), .4);
        std::ranges::fill(input.protection.view().row(y), 0);
    }
    const auto before = gamma_bits(input.luminance.view().storage());
    core::Budget budget{budget_limit};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    const auto gamma = methods::Gamma::create({.gamma = 1});
    const auto gamma_zero = methods::Gamma::create({.blend = 0});
    const auto zero = methods::Levels::create({.blend = 0});
    const auto levels = methods::Levels::create();
    REQUIRE(gamma);
    REQUIRE(gamma_zero);
    REQUIRE(zero);
    REQUIRE(levels);
    const auto identity = methods::apply(*gamma, {input.luminance.view(), {}, budget, scheduler});
    REQUIRE(identity);
    CHECK(identity->reason == methods::GammaReason::identity_gamma);
    CHECK(identity->counts.evaluated == 0);
    const auto bypass =
        methods::apply(*gamma_zero, {input.luminance.view(), {}, budget, scheduler});
    REQUIRE(bypass);
    CHECK(bypass->reason == methods::GammaReason::zero_blend);
    CHECK(bypass->counts.evaluated == 0);
    CHECK(gamma_bits(input.luminance.view().storage()) == before);
    for (const auto& method : {*zero, *levels}) {
        const auto facts = methods::apply(method, {input.luminance.view(), {}, budget, scheduler});
        REQUIRE(facts);
        CHECK(methods::validate(*facts, method.parameters()));
        CHECK(facts->counts.changed == 0);
        CHECK(facts->counts.evaluated == 0);
        CHECK(gamma_bits(input.luminance.view().storage()) == before);
    }
    CHECK(budget.used() == 0);
}
TEST_CASE("Levels baseline fits a genuine nonconstant narrow range without mapping") {
    GammaFixture input{8, 1};
    std::ranges::fill(input.protection.view().row(0), 0);
    for (std::uint32_t x = 0; x < 8; ++x) {
        input.luminance.view().row(0).subspan(x, 1).front() =
            0.25 + (static_cast<double>(x) * 1e-8);
    }
    input.protection.view().row(0).back() = 255;
    input.luminance.view().row(0).back() = .9;
    const auto before = gamma_bits(input.luminance.view().storage());
    const auto method = methods::Levels::create();
    REQUIRE(method);
    core::Budget budget{budget_limit};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    const auto facts = methods::apply(
        *method, {input.luminance.view(), input.protection.view().as_const(), budget, scheduler});
    REQUIRE(facts);
    REQUIRE(facts->levels.has_value());
    if (!facts->levels.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    CHECK(facts->reason == methods::LevelsReason::insufficient_dynamic_range);
    CHECK(facts->levels.value().high > facts->levels.value().low);
    CHECK(facts->levels.value().high - facts->levels.value().low < methods::levels_minimum_range);
    CHECK(facts->counts.eligible == 7);
    CHECK(facts->counts.evaluated == 0);
    CHECK(facts->counts.changed == 0);
    CHECK(methods::validate(*facts, method->parameters()));
    CHECK(gamma_bits(input.luminance.view().storage()) == before);
    CHECK(budget.used() == 0);
}
TEST_CASE("Contrast budgets and every bounded fitting stop refund owned storage", "[contrast]") {
    GammaFixture input{16, 8};
    for (std::uint32_t y = 0; y < 8; ++y) {
        std::ranges::fill(input.protection.view().row(y), 0);
    }
    const auto method = methods::Levels::create();
    REQUIRE(method);
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    core::Budget measured{budget_limit};
    REQUIRE(methods::apply(*method, {input.luminance.view(), {}, measured, scheduler}));
    const auto peak = measured.peak();
    CHECK(measured.used() == 0);
    REQUIRE(peak > 0);
    for (const bool enough : {true, false}) {
        core::Budget bounded{peak - static_cast<std::size_t>(!enough)};
        const auto before = gamma_bits(input.luminance.view().storage());
        const auto result =
            methods::apply(*method, {input.luminance.view(), {}, bounded, scheduler});
        if (enough) {
            REQUIRE(result);
        } else {
            REQUIRE(!result);
            CHECK(result.error().code == core::ErrorCode::resource);
            CHECK(gamma_bits(input.luminance.view().storage()) == before);
        }
        CHECK(bounded.used() == 0);
    }
    for (const auto phase : {core::Checkpoint::allocation, core::Checkpoint::measurement}) {
        const CheckpointStop observe{phase, std::numeric_limits<std::size_t>::max()};
        const exec::Scheduler observed{*concurrency, observe.cancellation()};
        REQUIRE(methods::apply(*method, {input.luminance.view(), {}, measured, observed}));
        const auto visits = CheckpointStop::visits();
        REQUIRE(visits > 0);
        for (std::size_t allowed = 0; allowed < visits; ++allowed) {
            const CheckpointStop stop{phase, allowed};
            const exec::Scheduler stopped{*concurrency, stop.cancellation()};
            const auto snapshot = gamma_bits(input.luminance.view().storage());
            const auto result =
                methods::apply(*method, {input.luminance.view(), {}, measured, stopped});
            REQUIRE(!result);
            CHECK(result.error().code == core::ErrorCode::cancelled);
            CHECK(measured.used() == 0);
            CHECK(gamma_bits(input.luminance.view().storage()) == snapshot);
        }
    }
}
TEST_CASE("Contrast application stops return no Facts and retain actual stores only",
          "[contrast]") {
    GammaFixture input{2051, 1};
    std::ranges::fill(input.luminance.view().row(0), .4);
    const auto before = gamma_bits(input.luminance.view().storage());
    const auto method = methods::Gamma::create({.gamma = 2});
    REQUIRE(method);
    core::Budget budget{budget_limit};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const CheckpointStop observe{core::Checkpoint::processing,
                                 std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler observed{*concurrency, observe.cancellation()};
    REQUIRE(methods::apply(*method, {input.luminance.view(), {}, budget, observed}));
    const auto visits = CheckpointStop::visits();
    REQUIRE(visits == 3);
    const auto completed = gamma_bits(input.luminance.view().storage());
    REQUIRE(completed.front() != before.front());
    for (std::size_t allowed = 0; allowed < visits; ++allowed) {
        std::ranges::fill(input.luminance.view().row(0), .4);
        const CheckpointStop stop{core::Checkpoint::processing, allowed};
        const exec::Scheduler stopped{*concurrency, stop.cancellation()};
        const auto result = methods::apply(*method, {input.luminance.view(), {}, budget, stopped});
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(budget.used() == 0);
        const auto after = gamma_bits(input.luminance.view().storage());
        for (std::size_t x = 0; x < after.size(); ++x) {
            const bool stored = x < 2051 && x / 1024 < allowed;
            CHECK(after.at(x) == (stored ? completed.at(x) : before.at(x)));
        }
    }
}
} // namespace docenhance::tests
