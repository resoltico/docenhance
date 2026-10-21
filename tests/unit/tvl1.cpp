// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/tvl1.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1_working.hpp"
#include "gamma_fixture.hpp"
#include "tvl1_fixture.hpp"
#include "tvl1_operators.hpp"
#include "tvl1_reference.hpp"
#include "tvl1_state_bounds.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>
namespace docenhance::tests {
namespace {
constexpr std::size_t budget_limit = std::size_t{2} * 1024 * 1024;
void compare_field(const TvPlanes& planes, const TvReference& reference) {
    for (std::size_t i = 0; i < reference.primal.size(); ++i) {
        const auto x = static_cast<unsigned>(i % reference.width);
        const auto y = static_cast<unsigned>(i / reference.width);
        const auto value = planes.u.view().row(y).subspan(x, 1).front();
        CHECK(std::isfinite(value));
        CHECK(std::abs(value - reference.primal[i]) < 1e-12);
        CHECK(planes.px.view().row(y).back() == 0);
        CHECK(planes.py.view().row(planes.py.height() - 1).subspan(x, 1).front() == 0);
    }
}
} // namespace
TEST_CASE(
    "TV-L1 simultaneous iterates and objectives match an independent scattered-adjoint reference",
    "[tvl1]") {
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    for (const auto extent : std::to_array<image::Extent>({
             {.width = 1, .height = 1},
             {.width = 1, .height = 9},
             {.width = 7, .height = 1},
             {.width = 3, .height = 5},
             {.width = 9, .height = 11},
         })) {
        core::Budget budget{budget_limit};
        TvPlanes planes{budget, extent};
        std::vector<double> values;
        values.reserve(std::size_t{extent.width} * extent.height);
        for (unsigned i = 0; i < extent.width * extent.height; ++i) {
            values.push_back(static_cast<double>((i * 17) % 101) / 100);
        }
        planes.fill(values);
        TvReference reference{extent.width, extent.height, values};
        constexpr double fidelity = 1.5;
        for (unsigned iteration = 0; iteration < 40; ++iteration) {
            CHECK(tvl1_state_bounded(planes.field(), values));
            REQUIRE(methods::tvl1_iteration(planes.field(), fidelity, scheduler));
            CHECK(tvl1_state_bounded(planes.field(), values));
            reference.iterate(fidelity);
            compare_field(planes, reference);
        }
        auto objective = methods::tvl1_objective(planes.f.view().as_const(),
                                                 planes.u.view().as_const(), fidelity, scheduler);
        REQUIRE(objective);
        CHECK(std::isfinite(*objective));
        CHECK(*objective >= 0);
        CHECK(std::abs(*objective - reference.objective(fidelity)) < 1e-10);
    }
}
TEST_CASE("TV-L1 forward gradient and exact adjoint retain terminal zero components", "[tvl1]") {
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    core::Budget budget{budget_limit};
    constexpr image::Extent extent{.width = 13, .height = 17};
    TvPlanes planes{budget, extent};
    std::vector<double> values;
    values.reserve(std::size_t{extent.width} * extent.height);
    for (unsigned i = 0; i < extent.width * extent.height; ++i) {
        values.push_back(static_cast<double>((i * 37) % 127) / 127);
    }
    planes.fill(values);
    double left = 0;
    double right = 0;
    for (unsigned y = 0; y < extent.height; ++y) {
        for (unsigned x = 0; x < extent.width; ++x) {
            planes.px.view().row(y).subspan(x, 1).front() = x + 1 < extent.width ? 0.2 : 0;
            planes.py.view().row(y).subspan(x, 1).front() = y + 1 < extent.height ? -0.3 : 0;
        }
    }
    for (unsigned y = 0; y < extent.height; ++y) {
        for (unsigned x = 0; x < extent.width; ++x) {
            const auto g = methods::tvl1_gradient(planes.u.view().as_const(), x, y);
            left += (g.x * planes.px.view().row(y).subspan(x, 1).front()) +
                    (g.y * planes.py.view().row(y).subspan(x, 1).front());
            right += planes.u.view().row(y).subspan(x, 1).front() *
                     methods::tvl1_adjoint(planes.px.view().as_const(), planes.py.view().as_const(),
                                           x, y);
        }
    }
    CHECK(std::abs(left - right) < 1e-10);
    planes.px.view().row(0).back() = 0.1;
    CHECK(!methods::tvl1_iteration(planes.field(), 1.5, scheduler));
}
TEST_CASE("TV-L1 constants and short caps report the actual stopping criterion", "[tvl1]") {
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    GammaFixture source{8, 4};
    for (std::uint32_t y = 0; y < 4; ++y) {
        std::ranges::fill(source.luminance.view().row(y), .4);
    }
    const auto before = gamma_bits(source.luminance.view().storage());
    core::Budget budget{budget_limit};
    for (const auto cap : {10U, 20U, 29U, 30U, 150U}) {
        const auto method = methods::Tvl1::create({.iterations = cap});
        REQUIRE(method);
        std::optional<methods::Tvl1Stop> observed;
        const auto facts =
            methods::apply(*method, {source.luminance.view(), {}, budget, scheduler}, observed);
        REQUIRE(facts);
        REQUIRE(facts->solver.has_value());
        if (!facts->solver.has_value()) {
            throw std::logic_error("Required test observation is absent");
        }
        CHECK(facts->solver.value().iterations == std::min(cap, 30U));
        CHECK(facts->solver.value().stop ==
              (cap < 30 ? methods::Tvl1Stop::iteration_limit : methods::Tvl1Stop::tolerance_met));
        CHECK(facts->solver.value().primal_update == 0);
        CHECK(facts->solver.value().dual_update == 0);
        CHECK(observed == facts->solver.value().stop);
        CHECK(facts->counts.evaluated == 32);
        CHECK(facts->counts.changed == 0);
        CHECK(gamma_bits(source.luminance.view().storage()) == before);
        CHECK(budget.used() == 0);
    }
}
TEST_CASE("TV-L1 budget refusal and cancellation preserve ownership without successful Facts",
          "[tvl1]") {
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    GammaFixture source{8, 4};
    for (std::uint32_t y = 0; y < 4; ++y) {
        std::ranges::fill(source.luminance.view().row(y), .4);
    }
    const auto before = gamma_bits(source.luminance.view().storage());
    const auto method = methods::Tvl1::create({.iterations = 30});
    REQUIRE(method);
    std::optional<methods::Tvl1Stop> observed;
    core::Budget measured{budget_limit};
    REQUIRE(methods::apply(*method, {source.luminance.view(), {}, measured, scheduler}, observed));
    CHECK(measured.used() == 0);
    const auto peak = measured.peak();
    REQUIRE(peak > 0);
    for (const bool enough : {true, false}) {
        core::Budget bounded{peak - static_cast<std::size_t>(!enough)};
        const auto result =
            methods::apply(*method, {source.luminance.view(), {}, bounded, scheduler}, observed);
        if (enough) {
            REQUIRE(result);
        } else {
            REQUIRE(!result);
            CHECK(result.error().code == core::ErrorCode::resource);
            CHECK(!observed);
        }
        CHECK(bounded.used() == 0);
        CHECK(gamma_bits(source.luminance.view().storage()) == before);
    }
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::measurement,
             core::Checkpoint::processing,
         }) {
        const CheckpointStop observe{phase, std::numeric_limits<std::size_t>::max()};
        const exec::Scheduler scheduled{*concurrency, observe.cancellation()};
        REQUIRE(
            methods::apply(*method, {source.luminance.view(), {}, measured, scheduled}, observed));
        const auto visits = CheckpointStop::visits();
        REQUIRE(visits > 0);
        for (std::size_t allowed = 0; allowed < visits; ++allowed) {
            const CheckpointStop stop{phase, allowed};
            const exec::Scheduler stopped{*concurrency, stop.cancellation()};
            const auto result =
                methods::apply(*method, {source.luminance.view(), {}, measured, stopped}, observed);
            REQUIRE(!result);
            CHECK(result.error().code == core::ErrorCode::cancelled);
            CHECK(measured.used() == 0);
            CHECK(gamma_bits(source.luminance.view().storage()) == before);
            if (phase == core::Checkpoint::allocation) {
                CHECK(!observed);
            }
        }
    }
}
TEST_CASE("TV-L1 lowers the defined noisy objective after 400 full-field reference iterations",
          "[tvl1]") {
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    core::Budget budget{budget_limit};
    constexpr image::Extent extent{.width = 32, .height = 32};
    TvPlanes planes{budget, extent};
    std::vector<double> values;
    values.reserve(std::size_t{extent.width} * extent.height);
    std::uint32_t state = 1729;
    for (unsigned i = 0; i < extent.width * extent.height; ++i) {
        state = (1664525 * state) + 1013904223;
        const auto noise = static_cast<int>((state >> 24U) & 255U) - 128;
        values.push_back(0.7 + (static_cast<double>(noise) / 4096));
    }
    planes.fill(values);
    constexpr double fidelity = 1.5;
    const auto before = methods::tvl1_objective(planes.f.view().as_const(),
                                                planes.u.view().as_const(), fidelity, scheduler);
    REQUIRE(before);
    CHECK(std::isfinite(*before));
    CHECK(*before >= 0);
    constexpr unsigned iterations = 400;
    for (unsigned i = 0; i < iterations; ++i) {
        CHECK(tvl1_state_bounded(planes.field(), values));
        REQUIRE(methods::tvl1_iteration(planes.field(), fidelity, scheduler));
        CHECK(tvl1_state_bounded(planes.field(), values));
    }
    const auto after = methods::tvl1_objective(planes.f.view().as_const(),
                                               planes.u.view().as_const(), fidelity, scheduler);
    REQUIRE(after);
    CHECK(std::isfinite(*after));
    CHECK(*after >= 0);
    CHECK(*after < *before);
    const auto output = planes.u.view();
    for (unsigned y = 0; y < extent.height; ++y) {
        const auto row = output.row(y);
        for (const auto u : row) {
            CHECK(std::isfinite(u));
            CHECK(u >= 0);
            CHECK(u <= 1);
        }
    }
}
TEST_CASE("TV-L1 perceptual blending uses luminance transport and exact identity", "[tvl1]") {
    const image::Rgb rgb{0.1, 0.3, 0.7};
    const double y = (0.2126 * rgb[0]) + (0.7152 * rgb[1]) + (0.0722 * rgb[2]);
    const auto f = image::srgb_encode(y);
    CHECK(image::transport_luminance(rgb, y) == rgb);
    const auto target = image::srgb_decode(f + 0.05);
    const auto result = image::transport_luminance(rgb, target);
    for (const auto channel : result) {
        CHECK(std::isfinite(channel));
        CHECK(channel >= 0);
        CHECK(channel <= 1);
    }
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        const auto expected = rgb.at(i) + (((target - y) / (1 - y)) * (1 - rgb.at(i)));
        CHECK(std::abs(result.at(i) - expected) < 1e-12);
    }
}
} // namespace docenhance::tests
