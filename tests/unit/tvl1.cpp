// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/tvl1.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "linear_fixture.hpp"
#include "tvl1_operators.hpp"
#include "tvl1_reference.hpp"

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
struct TvPlanes {
    image::Plane<double> f;
    image::Plane<double> u;
    image::Plane<double> bar;
    image::Plane<double> px;
    image::Plane<double> py;
    TvPlanes(core::Budget& budget, image::Extent extent)
        : f(image::Plane<double>::allocate(budget, extent.width, extent.height).value()),
          u(image::Plane<double>::allocate(budget, extent.width, extent.height).value()),
          bar(image::Plane<double>::allocate(budget, extent.width, extent.height).value()),
          px(image::Plane<double>::allocate(budget, extent.width, extent.height).value()),
          py(image::Plane<double>::allocate(budget, extent.width, extent.height).value()) {}
    methods::Tvl1Field field() {
        return {
            .input = f.view().as_const(),
            .primal = u.view(),
            .extrapolated = bar.view(),
            .dual_x = px.view(),
            .dual_y = py.view(),
        };
    }
    void fill(const std::vector<double>& values) {
        for (unsigned y = 0; y < f.height(); ++y) {
            for (unsigned x = 0; x < f.width(); ++x) {
                const auto value = values[(std::size_t{y} * f.width()) + x];
                f.view().row(y).subspan(x, 1).front() = value;
                u.view().row(y).subspan(x, 1).front() = value;
                bar.view().row(y).subspan(x, 1).front() = value;
                px.view().row(y).subspan(x, 1).front() = 0;
                py.view().row(y).subspan(x, 1).front() = 0;
            }
        }
    }
};
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
            REQUIRE(methods::tvl1_iteration(planes.field(), fidelity));
            reference.iterate(fidelity);
            compare_field(planes, reference);
        }
        auto objective = methods::tvl1_objective(planes.f.view().as_const(),
                                                 planes.u.view().as_const(), fidelity);
        REQUIRE(objective);
        CHECK(std::abs(*objective - reference.objective(fidelity)) < 1e-10);
    }
}
TEST_CASE("TV-L1 forward gradient and exact adjoint retain terminal zero components", "[tvl1]") {
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
    CHECK(!methods::tvl1_iteration(planes.field(), 1.5));
    planes.px.view().row(0).back() = 0;
    planes.bar.view().row(0).front() = std::numeric_limits<double>::quiet_NaN();
    CHECK(!methods::tvl1_iteration(planes.field(), 1.5));
}
TEST_CASE("TV-L1 constants and short caps report the actual stopping criterion", "[tvl1]") {
    core::Budget budget{budget_limit};
    LinearFixture source{budget, {.width = 8, .height = 4}};
    source.fill(0.4);
    for (const auto cap : {10U, 20U, 29U, 30U, 150U}) {
        methods::DenoisingReport report;
        const auto method = methods::Tvl1::create({.iterations = cap}).value();
        auto model =
            methods::Tvl1Model::prepare(source, {}, method, {budget, core::Cancellation{}, report});
        REQUIRE(model);
        REQUIRE(report.tvl1);
        if (report.tvl1) {
            CHECK(report.tvl1->iterations == std::min(cap, 30U));
            CHECK(report.tvl1->stop == (cap < 30 ? methods::Tvl1Stop::iteration_limit
                                                 : methods::Tvl1Stop::tolerance_met));
            CHECK(report.tvl1->primal_update == 0);
            CHECK(report.tvl1->dual_update == 0);
        }
        CHECK(model->input().row(0).front() == model->result().row(0).front());
    }
}
TEST_CASE("TV-L1 budget refusal and cancellation preserve ownership and truthful observations",
          "[tvl1]") {
    core::Budget source_budget{budget_limit};
    LinearFixture source{source_budget, {.width = 8, .height = 4}};
    source.fill(0.4);
    const auto method = methods::Tvl1::create({.iterations = 30}).value();
    const core::Cancellation cancellation;
    methods::DenoisingReport report;
    core::Budget measured{budget_limit};
    {
        auto model =
            methods::Tvl1Model::prepare(source, {}, method, {measured, cancellation, report});
        REQUIRE(model);
    }
    CHECK(measured.used() == 0);
    const auto peak = static_cast<std::size_t>(report.preparation_charge_peak);
    core::Budget exact{peak};
    {
        REQUIRE(methods::Tvl1Model::prepare(source, {}, method, {exact, cancellation, report}));
    }
    CHECK(exact.used() == 0);
    core::Budget refused{peak - 1};
    CHECK(!methods::Tvl1Model::prepare(source, {}, method, {refused, cancellation, report}));
    CHECK(refused.used() == 0);
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::measurement,
             core::Checkpoint::processing,
         }) {
        std::size_t visits = 0;
        {
            const CheckpointStop observe{phase, std::numeric_limits<std::size_t>::max()};
            REQUIRE(methods::Tvl1Model::prepare(source, {}, method,
                                                {measured, observe.cancellation(), report}));
            visits = CheckpointStop::visits();
        }
        for (std::size_t allowed = 0; allowed < visits; ++allowed) {
            const CheckpointStop stop{phase, allowed};
            auto model = methods::Tvl1Model::prepare(source, {}, method,
                                                     {measured, stop.cancellation(), report});
            REQUIRE(!model);
            CHECK(model.error().code == core::ErrorCode::cancelled);
            CHECK(measured.used() == 0);
            CHECK(methods::valid_tvl1_observations(report));
        }
    }
}
TEST_CASE("TV-L1 lowers the defined noisy objective after 400 full-field reference iterations",
          "[tvl1]") {
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
    const auto before =
        methods::tvl1_objective(planes.f.view().as_const(), planes.u.view().as_const(), fidelity);
    REQUIRE(before);
    constexpr unsigned iterations = 400;
    for (unsigned i = 0; i < iterations; ++i) {
        REQUIRE(methods::tvl1_iteration(planes.field(), fidelity));
    }
    const auto after =
        methods::tvl1_objective(planes.f.view().as_const(), planes.u.view().as_const(), fidelity);
    REQUIRE(after);
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
    const auto f = image::srgb_encode(y).value();
    CHECK(methods::tvl1_correct(rgb, f, f, 1).value() == rgb);
    CHECK(methods::tvl1_correct(rgb, f, f + 0.1, 0).value() == rgb);
    const auto target = image::srgb_decode(f + 0.05).value();
    const auto result = methods::tvl1_correct(rgb, f, f + 0.1, 0.5);
    REQUIRE(result);
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        const auto expected = rgb.at(i) + (((target - y) / (1 - y)) * (1 - rgb.at(i)));
        CHECK(std::abs(result->at(i) - expected) < 1e-12);
    }
    CHECK(!methods::tvl1_correct(rgb, f, std::numeric_limits<double>::quiet_NaN(), 1));
}
} // namespace docenhance::tests
