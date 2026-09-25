// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>
namespace docenhance::tests {
// Independent dense edge assembly and pivoted Gaussian elimination. No production operator,
// preconditioner, reduction, interpolation or PCG recurrence is used by this oracle.
struct DenseSurface {
    std::size_t columns{};
    std::size_t rows{};
    std::span<const double> weights;
    std::span<const double> measured;
    double smooth{};
};
inline std::vector<double> surface_matrix(const DenseSurface& problem) {
    const auto n = problem.weights.size();
    std::vector<double> matrix(n * n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        matrix.at((i * n) + i) += problem.weights.subspan(i, 1).front();
        const auto connect = [&](std::size_t j) {
            matrix.at((i * n) + i) += problem.smooth;
            matrix.at((j * n) + j) += problem.smooth;
            matrix.at((i * n) + j) -= problem.smooth;
            matrix.at((j * n) + i) -= problem.smooth;
        };
        if ((i % problem.columns) + 1 < problem.columns) {
            connect(i + 1);
        }
        if ((i / problem.columns) + 1 < problem.rows) {
            connect(i + problem.columns);
        }
    }
    return matrix;
}
inline std::size_t surface_pivot(const std::vector<double>& matrix, std::size_t n, std::size_t i) {
    auto pivot = i;
    for (auto row = i + 1; row < n; ++row) {
        if (std::abs(matrix.at((row * n) + i)) > std::abs(matrix.at((pivot * n) + i))) {
            pivot = row;
        }
    }
    return pivot;
}
inline std::vector<double> dense_surface_solution(const DenseSurface& problem) {
    const auto n = problem.weights.size();
    auto matrix = surface_matrix(problem);
    std::vector<double> rhs(n);
    for (std::size_t i = 0; i < n; ++i) {
        rhs.at(i) = problem.weights.subspan(i, 1).front() * problem.measured.subspan(i, 1).front();
    }
    for (std::size_t i = 0; i < n; ++i) {
        const auto pivot = surface_pivot(matrix, n, i);
        for (std::size_t c = 0; c < n; ++c) {
            std::swap(matrix.at((i * n) + c), matrix.at((pivot * n) + c));
        }
        std::swap(rhs.at(i), rhs.at(pivot));
        const double diagonal = matrix.at((i * n) + i);
        if (diagonal == 0) {
            return {};
        }
        for (auto c = i; c < n; ++c) {
            matrix.at((i * n) + c) /= diagonal;
        }
        rhs.at(i) /= diagonal;
        for (auto row = i + 1; row < n; ++row) {
            const double factor = matrix.at((row * n) + i);
            for (auto c = i; c < n; ++c) {
                matrix.at((row * n) + c) -= factor * matrix.at((i * n) + c);
            }
            rhs.at(row) -= factor * rhs.at(i);
        }
    }
    for (auto i = n; i != 0; --i) {
        for (auto j = i; j < n; ++j) {
            rhs.at(i - 1) -= matrix.at(((i - 1) * n) + j) * rhs.at(j);
        }
    }
    return rhs;
}
} // namespace docenhance::tests
