// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/methods/tvl1_working.hpp"
#include "docenhance/methods/working.hpp"
#include "gamma_fixture.hpp"
#include "tvl1_fixture.hpp"
#include "tvl1_state_bounds.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <vector>

namespace docenhance::tests {
namespace {
constexpr std::size_t state_budget = std::size_t{2} * 1024 * 1024;
// The eager operation borrows its live scheduler, not a retained lazy model.
static_assert(std::is_constructible_v<methods::Working, image::PlaneView<double>,
                                      image::PlaneView<const std::uint8_t>, core::Budget&,
                                      const exec::Scheduler&>);
static_assert(!std::is_constructible_v<methods::Working, image::PlaneView<double>,
                                       image::PlaneView<const std::uint8_t>, core::Budget&,
                                       exec::Scheduler&&>);
void healthy_iteration(TvPlanes& planes, const std::vector<double>& values) {
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    REQUIRE(tvl1_state_bounded(planes.field(), values));
    REQUIRE(methods::tvl1_iteration(planes.field(), 1.5, scheduler));
    REQUIRE(tvl1_state_bounded(planes.field(), values));
}
void check_late_terminal(const core::Result<methods::Tvl1Updates>& result,
                         std::span<const double> terminal, bool cancelled) {
    REQUIRE(!result);
    CHECK(result.error().code ==
          (cancelled ? core::ErrorCode::cancelled : core::ErrorCode::numerical));
    CHECK(terminal.back() == 0.1);
    if (cancelled) {
        CHECK(CheckpointStop::visits() == 3);
    } else {
        CHECK(result.error().message == "Invalid TV-L1 extrapolated or dual sample");
    }
}
void late_terminal_case(bool horizontal, bool cancelled) {
    core::Budget budget{state_budget};
    TvPlanes planes{budget, {.width = 2051, .height = 1}};
    const std::vector<double> values(2051, 0.4);
    planes.fill(values);
    healthy_iteration(planes, values);
    planes.fill(values);
    const auto terminal = horizontal ? planes.px.view().row(0) : planes.py.view().row(0);
    terminal.back() = 0.1;
    const CheckpointStop stop{core::Checkpoint::processing,
                              cancelled ? 2 : std::numeric_limits<std::size_t>::max()};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency, stop.cancellation()};
    const auto result = methods::tvl1_iteration(planes.field(), 1.5, scheduler);
    check_late_terminal(result, terminal, cancelled);
}
void pending_terminal_case(bool horizontal) {
    core::Budget budget{state_budget};
    TvPlanes planes{budget, {.width = 2051, .height = 1}};
    const std::vector<double> values(2051, 0.4);
    planes.fill(values);
    const auto terminal = horizontal ? planes.px.view().row(0) : planes.py.view().row(0);
    terminal.back() = 0.1;
    std::stop_source stop;
    request_stop(stop);
    const core::Cancellation pending{stop.get_token()};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler pending_scheduler{*concurrency, pending};
    const auto result = methods::tvl1_iteration(planes.field(), 1.5, pending_scheduler);
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::cancelled);
    CHECK(terminal.back() == 0.1);
}
methods::Tvl1Field field_from(const std::array<image::PlaneView<double>, 5>& views) {
    return {
        .input = views.at(0).as_const(),
        .primal = views.at(1),
        .extrapolated = views.at(2),
        .dual_x = views.at(3),
        .dual_y = views.at(4),
    };
}
} // namespace
TEST_CASE("TV-L1 public terminal poison stays unconsumed past interior cancellation") {
    late_terminal_case(true, true);
    late_terminal_case(false, true);
    late_terminal_case(true, false);
    late_terminal_case(false, false);
    pending_terminal_case(true);
    pending_terminal_case(false);
}
TEST_CASE("TV-L1 all ten mutable role aliases and fidelity reject before pending stop") {
    core::Budget budget{state_budget};
    TvPlanes planes{budget, {.width = 3, .height = 2}};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    const std::vector<double> values(6, 0);
    planes.fill(values);
    REQUIRE(methods::tvl1_iteration(planes.field(), 1.5, scheduler));
    const std::array roles{
        planes.f.view(), planes.u.view(), planes.bar.view(), planes.px.view(), planes.py.view(),
    };
    std::stop_source stop;
    request_stop(stop);
    const core::Cancellation pending{stop.get_token()};
    const exec::Scheduler pending_scheduler{*concurrency, pending};
    std::size_t tested = 0;
    for (std::size_t first = 0; first < roles.size(); ++first) {
        for (auto second = first + 1; second < roles.size(); ++second) {
            auto aliases = roles;
            aliases.at(second) = aliases.at(first);
            const auto result =
                methods::tvl1_iteration(field_from(aliases), 1.5, pending_scheduler);
            REQUIRE(!result);
            CHECK(result.error().code == core::ErrorCode::argument);
            ++tested;
        }
    }
    CHECK(tested == 10);
    for (const auto lambda : {0.01, 20.01, std::numeric_limits<double>::quiet_NaN()}) {
        const auto invalid = methods::tvl1_iteration(planes.field(), lambda, pending_scheduler);
        REQUIRE(!invalid);
        CHECK(invalid.error().code == core::ErrorCode::argument);
    }
    const auto cancelled = methods::tvl1_iteration(planes.field(), 1.5, pending_scheduler);
    REQUIRE(!cancelled);
    CHECK(cancelled.error().code == core::ErrorCode::cancelled);
}
TEST_CASE("TV-L1 readonly objective alias preserves positive ordered energy") {
    core::Budget budget{state_budget};
    TvPlanes planes{budget, {.width = 7, .height = 1}};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    const std::vector<double> values{0.4, 0.4001, 0.4002, 0.4003, 0.4004, 0.4005, 0.4006};
    planes.fill(values);
    const auto separate = methods::tvl1_objective(planes.f.view().as_const(),
                                                  planes.u.view().as_const(), 20, scheduler);
    const auto alias = methods::tvl1_objective(planes.f.view().as_const(),
                                               planes.f.view().as_const(), 20, scheduler);
    REQUIRE(separate);
    REQUIRE(alias);
    CHECK(std::isfinite(*separate));
    CHECK(*separate > 0);
    CHECK(*alias == *separate);
}
TEST_CASE("TV-L1 high fidelity preserves primal while real dual motion prevents false stop") {
    GammaFixture source{7, 1};
    for (std::uint32_t x = 0; x < 7; ++x) {
        source.luminance.view().row(0).subspan(x, 1).front() =
            image::srgb_decode(0.4 + (static_cast<double>(x) * 1e-4));
    }
    const auto before = gamma_bits(source.luminance.view().storage());
    core::Budget budget{state_budget};
    const auto method =
        methods::Tvl1::create({.lambda = 20, .iterations = 30, .tolerance = 1e-8, .blend = 1});
    REQUIRE(method);
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    std::optional<methods::Tvl1Stop> observed;
    const auto facts =
        methods::apply(*method, {source.luminance.view(), {}, budget, scheduler}, observed);
    REQUIRE(facts);
    REQUIRE(facts->solver.has_value());
    if (!facts->solver.has_value()) {
        throw std::logic_error("Required TV-L1 solver observation is absent");
    }
    CHECK(gamma_bits(source.luminance.view().storage()) == before);
    CHECK(facts->solver.value().iterations == 30);
    CHECK(facts->solver.value().stop == methods::Tvl1Stop::iteration_limit);
    CHECK(observed == facts->solver.value().stop);
    CHECK(facts->solver.value().primal_update == 0);
    CHECK(facts->solver.value().dual_update > method->parameters().tolerance);
    CHECK(std::isfinite(facts->solver.value().objective_start));
    CHECK(facts->solver.value().objective_start > 0);
    CHECK(facts->solver.value().objective_end == facts->solver.value().objective_start);
    CHECK(facts->counts.evaluated == 7);
    CHECK(facts->counts.changed == 0);
    CHECK(budget.used() == 0);
}
TEST_CASE("TV-L1 cold structural admission precedes no-op and pending cancellation") {
    GammaFixture source{7, 1};
    std::ranges::fill(source.luminance.view().row(0), .4);
    const auto before = gamma_bits(source.luminance.view().storage());
    const auto method = methods::Tvl1::create();
    const auto zero = methods::Tvl1::create({.blend = 0});
    REQUIRE(method);
    REQUIRE(zero);
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    core::Budget budget{state_budget};
    std::optional<methods::Tvl1Stop> observed;
    const auto healthy =
        methods::apply(*method, {source.luminance.view(), {}, budget, scheduler}, observed);
    REQUIRE(healthy);
    REQUIRE(healthy->solver.has_value());
    CHECK(budget.used() == 0);
    core::Budget cold{0};
    const auto bypass =
        methods::apply(*zero, {source.luminance.view(), {}, cold, scheduler}, observed);
    REQUIRE(bypass);
    CHECK(bypass->reason == methods::Tvl1Reason::zero_blend);
    CHECK(!bypass->solver);
    CHECK(!observed);
    std::stop_source stop;
    request_stop(stop);
    const exec::Scheduler pending{*concurrency, core::Cancellation{stop.get_token()}};
    const auto empty = methods::apply(*zero, {{}, {}, cold, pending}, observed);
    REQUIRE(!empty);
    CHECK(empty.error().code == core::ErrorCode::argument);
    const auto wrong_mask = image::PlaneView<const std::uint8_t>::create(
        source.protection.view().row(0).first(1), {.width = 1, .height = 1, .stride = 1});
    REQUIRE(wrong_mask);
    const auto mismatch =
        methods::apply(*zero, {source.luminance.view(), *wrong_mask, cold, pending}, observed);
    REQUIRE(!mismatch);
    CHECK(mismatch.error().code == core::ErrorCode::argument);
    const auto cancelled =
        methods::apply(*method, {source.luminance.view(), {}, cold, pending}, observed);
    REQUIRE(!cancelled);
    CHECK(cancelled.error().code == core::ErrorCode::cancelled);
    CHECK(!observed);
    CHECK(cold.used() == 0);
    CHECK(gamma_bits(source.luminance.view().storage()) == before);
}
} // namespace docenhance::tests
