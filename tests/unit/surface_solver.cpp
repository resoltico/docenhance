// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"
#include "surface_reference.hpp"

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
constexpr std::size_t solver_budget = 65536;
constexpr std::uint32_t solver_rows = 7;
constexpr double comparison_tolerance = 1e-7;
void check_operator(const methods::SurfaceSystem& system, const DenseSurface& reference,
                    const std::vector<double>& values) {
    const auto n = values.size();
    const auto matrix = surface_matrix(reference);
    std::vector<double> actual(n);
    REQUIRE(methods::apply_surface_system(system, values, actual, {}));
    for (std::size_t i = 0; i < n; ++i) {
        double expected = 0;
        for (std::size_t j = 0; j < n; ++j) {
            expected += matrix.at((i * n) + j) * values.at(j);
        }
        CHECK(std::abs(actual.at(i) - expected) < comparison_tolerance);
    }
}
void check_solution(const DenseSurface& reference, const std::vector<double>& answer,
                    const std::vector<double>& exact, const methods::SolverReport& report) {
    const auto n = answer.size();
    const auto matrix = surface_matrix(reference);
    const auto& weights = reference.weights;
    const auto& values = reference.measured;
    double true_residual = 0;
    for (std::size_t i = 0; i < n; ++i) {
        CHECK(std::abs(answer.at(i) - exact.at(i)) < comparison_tolerance);
        double r = -weights.subspan(i, 1).front() * values.subspan(i, 1).front();
        for (std::size_t j = 0; j < n; ++j) {
            r += matrix.at((i * n) + j) * answer.at(j);
        }
        true_residual += r * r;
    }
    CHECK(std::sqrt(true_residual) <= report.tolerance + std::numeric_limits<double>::epsilon());
    CHECK(report.residual <= report.tolerance);
}
void compare_system(std::uint32_t columns, std::uint32_t rows) {
    core::Budget budget{solver_budget};
    const auto n = columns * rows;
    auto workspace = image::Plane<double>::allocate(budget, n, solver_rows).value();
    std::vector<double> weights(n);
    std::vector<double> values(n);
    std::vector<double> answer(n);
    constexpr double weight_step = 0.25;
    constexpr double value_step = -0.125;
    constexpr double beta = 2;
    constexpr unsigned missing_period = 3;
    for (std::uint32_t i = 0; i < n; ++i) {
        weights.at(i) = i % missing_period == 0 ? 0 : weight_step;
        if (i == 0) {
            weights.at(i) = 1;
        }
        values.at(i) = weights.at(i) == 0 ? 0 : value_step * (i + 1);
    }
    std::ranges::copy(weights, workspace.view().row(0).begin());
    std::ranges::copy(values, workspace.view().row(1).begin());
    const DenseSurface reference{
        .columns = columns,
        .rows = rows,
        .weights = weights,
        .measured = values,
        .smooth = beta,
    };
    const methods::SurfaceSystem system{
        .grid = {.extent = {.width = columns, .height = rows}, .columns = columns, .rows = rows},
        .weights = weights,
        .smooth = beta,
    };
    check_operator(system, reference, values);
    methods::SolverReport report;
    REQUIRE(methods::solve_surface(system, workspace.view(), answer, report, {}));
    const auto exact = dense_surface_solution(reference);
    REQUIRE(exact.size() == n);
    check_solution(reference, answer, exact, report);
}
} // namespace
TEST_CASE("Log-surface operator and PCG agree with independently assembled dense systems",
          "[surface][solver]") {
    constexpr auto extents = std::to_array<std::uint32_t>({1, 2, 3, 4});
    for (auto const columns : extents) {
        for (auto const rows : extents) {
            compare_system(columns, rows);
        }
    }
}
TEST_CASE("Log-surface zero RHS, cancellation and inadmissible systems are distinct",
          "[surface][solver]") {
    core::Budget budget{solver_budget};
    auto work = image::Plane<double>::allocate(budget, 2, solver_rows).value();
    std::ranges::fill(work.view().storage(), 0);
    work.view().row(0).front() = 1;
    const methods::SurfaceSystem system{
        .grid = {.extent = {.width = 2, .height = 1}, .columns = 2, .rows = 1},
        .weights = work.view().row(0),
        .smooth = 1,
    };
    std::array<double, 2> output{};
    methods::SolverReport report;
    REQUIRE(methods::solve_surface(system, work.view(), output, report, {}));
    CHECK(report.iterations == 0);
    CHECK(report.residual == 0);
    work.view().row(1).front() = -1;
    CheckpointStop const stop{core::Checkpoint::solving, 0};
    auto cancelled =
        methods::solve_surface(system, work.view(), output, report, stop.cancellation());
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().code == core::ErrorCode::cancelled);
    work.view().row(0).front() = 0;
    work.view().row(1).front() = 0;
    auto empty = methods::solve_surface(system, work.view(), output, report, {});
    REQUIRE_FALSE(empty);
    CHECK(empty.error().code == core::ErrorCode::method_inapplicable);
    auto bad_system = system;
    bad_system.grid.columns = 0;
    CHECK_FALSE(methods::apply_surface_system(bad_system, output, output, {}));
}

TEST_CASE("A poorly conditioned measured grid fails its finite iteration contract",
          "[surface][solver]") {
    constexpr std::uint32_t size = 2048;
    constexpr std::uint32_t measured = size / 4;
    core::Budget budget{std::size_t{1024} * 1024};
    auto work = image::Plane<double>::allocate(budget, size, solver_rows).value();
    std::ranges::fill(work.view().storage(), 0);
    for (std::uint32_t i = 0; i < measured; ++i) {
        work.view().row(0).subspan(i, 1).front() = 1;
        work.view().row(1).subspan(i, 1).front() = -1 - (static_cast<double>(i) / measured);
    }
    const methods::SurfaceSystem system{
        .grid = {.extent = {.width = size, .height = 1}, .columns = size, .rows = 1},
        .weights = work.view().row(0),
        .smooth = 2,
    };
    std::vector<double> output(size);
    methods::SolverReport report;
    const auto result = methods::solve_surface(system, work.view(), output, report, {});
    REQUIRE_FALSE(result);
    CHECK(result.error().code == core::ErrorCode::numerical);
    CHECK(report.iterations == methods::surface_iteration_limit);
    CHECK(std::isfinite(report.residual));
    CHECK(report.residual > report.tolerance);
}
} // namespace docenhance::tests
