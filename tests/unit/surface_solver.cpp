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
// Cholesky factorization succeeds with positive pivots exactly for symmetric positive definite
// matrices; symmetry is checked separately to rounding precision.
bool symmetric_positive_definite(std::vector<double> matrix, std::size_t n) {
    constexpr double symmetry_tolerance = 1e-12;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (std::abs(matrix.at((i * n) + j) - matrix.at((j * n) + i)) > symmetry_tolerance) {
                return false;
            }
        }
    }
    for (std::size_t k = 0; k < n; ++k) {
        double pivot = matrix.at((k * n) + k);
        for (std::size_t j = 0; j < k; ++j) {
            pivot -= matrix.at((k * n) + j) * matrix.at((k * n) + j);
        }
        if (!(pivot > 0)) {
            return false;
        }
        const double root = std::sqrt(pivot);
        matrix.at((k * n) + k) = root;
        for (std::size_t i = k + 1; i < n; ++i) {
            double value = matrix.at((i * n) + k);
            for (std::size_t j = 0; j < k; ++j) {
                value -= matrix.at((i * n) + j) * matrix.at((k * n) + j);
            }
            matrix.at((i * n) + k) = value / root;
        }
    }
    return true;
}
// The dense matrix of one V-cycle, column by column, on a grid with a third of cells unmeasured.
std::vector<double> preconditioner_matrix(std::uint32_t columns, std::uint32_t rows) {
    const auto n = std::size_t{columns} * rows;
    std::vector<double> weights(n);
    for (std::size_t i = 0; i < n; ++i) {
        constexpr unsigned missing_period = 3;
        constexpr unsigned weight_levels = 5;
        constexpr double weight_step = 0.125;
        weights.at(i) = i % missing_period == 1
                            ? 0
                            : 0.25 + (weight_step * static_cast<double>(i % weight_levels));
    }
    core::Budget budget{solver_budget};
    const methods::SurfaceSystem system{
        .grid = {.extent = {.width = columns, .height = rows}, .columns = columns, .rows = rows},
        .weights = weights,
        .smooth = 2,
    };
    auto hierarchy = methods::SurfaceHierarchy::build(system, budget);
    REQUIRE(hierarchy);
    CHECK(hierarchy->levels().back().columns == 1);
    CHECK(hierarchy->levels().back().rows == 1);
    std::vector<double> matrix(n * n);
    std::vector<double> basis(n);
    std::vector<double> column(n);
    for (std::size_t j = 0; j < n; ++j) {
        std::ranges::fill(basis, 0);
        basis.at(j) = 1;
        REQUIRE(hierarchy->precondition(basis, column, {}));
        for (std::size_t i = 0; i < n; ++i) {
            matrix.at((i * n) + j) = column.at(i);
        }
    }
    return matrix;
}
void compare_system(std::uint32_t columns, std::uint32_t rows) {
    core::Budget budget{solver_budget};
    const core::Cancellation none;
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
    REQUIRE(methods::solve_surface(system, workspace.view(), answer, report,
                                   {.budget = budget, .cancellation = none}));
    const auto exact = dense_surface_solution(reference);
    REQUIRE(exact.size() == n);
    check_solution(reference, answer, exact, report);
}
} // namespace
TEST_CASE("Log-surface operator and multigrid PCG agree with independent dense systems",
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
    const core::Cancellation none;
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
    REQUIRE(methods::solve_surface(system, work.view(), output, report,
                                   {.budget = budget, .cancellation = none}));
    CHECK(report.iterations == 0);
    CHECK(report.residual == 0);
    work.view().row(1).front() = -1;
    CheckpointStop const stop{core::Checkpoint::solving, 0};
    const auto stopping = stop.cancellation();
    auto cancelled = methods::solve_surface(system, work.view(), output, report,
                                            {.budget = budget, .cancellation = stopping});
    REQUIRE(!cancelled);
    CHECK(cancelled.error().code == core::ErrorCode::cancelled);
    work.view().row(0).front() = 0;
    work.view().row(1).front() = 0;
    auto empty = methods::solve_surface(system, work.view(), output, report,
                                        {.budget = budget, .cancellation = none});
    REQUIRE(!empty);
    CHECK(empty.error().code == core::ErrorCode::method_inapplicable);
    auto bad_system = system;
    bad_system.grid.columns = 0;
    CHECK(!methods::apply_surface_system(bad_system, output, output, {}));
}

TEST_CASE("An unmet residual bound within the iteration limit is a numerical failure",
          "[surface][solver]") {
    constexpr std::uint32_t size = 2048;
    constexpr std::uint32_t measured = size / 4;
    constexpr unsigned limit = 2;
    core::Budget budget{std::size_t{1024} * 1024};
    const core::Cancellation none;
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
        .iteration_limit = limit,
    };
    std::vector<double> output(size);
    methods::SolverReport report;
    const auto held = budget.used();
    const auto result = methods::solve_surface(system, work.view(), output, report,
                                               {.budget = budget, .cancellation = none});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::numerical);
    CHECK(report.iterations == limit);
    CHECK(std::isfinite(report.residual));
    CHECK(report.residual > report.tolerance);
    CHECK(budget.used() == held);
}
TEST_CASE("The multigrid V-cycle is a symmetric positive definite preconditioner",
          "[surface][solver]") {
    constexpr auto extents = std::to_array<std::uint32_t>({1, 2, 3, 5, 7});
    for (auto const columns : extents) {
        for (auto const rows : extents) {
            CHECK(symmetric_positive_definite(preconditioner_matrix(columns, rows),
                                              std::size_t{columns} * rows));
        }
    }
}
TEST_CASE("Multigrid PCG converges within a small bound on wide unmeasured regions",
          "[surface][solver]") {
    struct Hole {
        std::uint32_t columns;
        std::uint32_t rows;
        std::uint32_t left;
        std::uint32_t right;
        std::uint32_t top;
        std::uint32_t bottom;
    };
    // Square hole, full-height band and a single-row grid: Jacobi needed 500 to 20000+ iterations.
    constexpr auto holes = std::to_array<Hole>({
        {.columns = 256, .rows = 256, .left = 28, .right = 228, .top = 28, .bottom = 228},
        {.columns = 512, .rows = 128, .left = 66, .right = 446, .top = 0, .bottom = 128},
        {.columns = 65536, .rows = 1, .left = 8000, .right = 56000, .top = 0, .bottom = 1},
    });
    constexpr unsigned bounded_iterations = 120;
    constexpr double maximum_smooth = 20;
    for (const auto& hole : holes) {
        const auto n = hole.columns * hole.rows;
        core::Budget budget{std::size_t{32} * 1024 * 1024};
        const core::Cancellation none;
        auto work = image::Plane<double>::allocate(budget, n, solver_rows).value();
        std::ranges::fill(work.view().storage(), 0);
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto x = i % hole.columns;
            const auto y = i / hole.columns;
            const bool missing =
                x >= hole.left && x < hole.right && y >= hole.top && y < hole.bottom;
            if (!missing) {
                work.view().row(0).subspan(i, 1).front() = 0.25 + (0.75 * ((i * 7) % 11) / 10);
                work.view().row(1).subspan(i, 1).front() = -0.1 - (0.9 * ((i * 5) % 13) / 12);
            }
        }
        const methods::SurfaceSystem system{
            .grid =
                {
                    .extent = {.width = hole.columns, .height = hole.rows},
                    .columns = hole.columns,
                    .rows = hole.rows,
                },
            .weights = work.view().row(0),
            .smooth = maximum_smooth,
        };
        std::vector<double> output(n);
        methods::SolverReport report;
        REQUIRE(methods::solve_surface(system, work.view(), output, report,
                                       {.budget = budget, .cancellation = none}));
        CHECK(report.iterations <= bounded_iterations);
        CHECK(report.residual <= report.tolerance);
    }
}
} // namespace docenhance::tests
