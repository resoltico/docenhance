// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace docenhance::methods {
namespace {
constexpr unsigned rhs_row = 2;
constexpr unsigned residual_row = 3;
constexpr unsigned direction_row = 4;
constexpr unsigned product_row = 5;
constexpr unsigned preconditioned_row = 6;
constexpr unsigned solver_rows = 7;
constexpr double rhs_floor = 1e-12;
// Fixed row-major reduction, not a backend-dependent parallel transform_reduce.
double dot(std::span<const double> left, std::span<const double> right) noexcept {
    double result = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        result += surface_at(left, i) * surface_at(right, i);
    }
    return result;
}
core::Result<void> initialize(const SurfaceSystem& system, image::PlaneView<double> work,
                              std::span<double> logarithms) {
    const auto count = system.weights.size();
    if (work.width() != count || work.height() != solver_rows || logarithms.size() != count ||
        count == 0 || count > surface_cell_limit) {
        return core::failure(core::ErrorCode::argument, "Invalid log-surface solver workspace");
    }
    auto const rhs = work.row(rhs_row);
    const auto measured = work.row(1);
    double sum_weights = 0;
    double sum_values = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const double weight = surface_at(system.weights, i);
        const double value = surface_at(measured, i);
        if (!std::isfinite(weight) || weight < 0 || weight > 1 || !std::isfinite(value) ||
            (weight == 0 && value != 0)) {
            return core::failure(core::ErrorCode::argument, "Invalid weighted log measurements");
        }
        surface_at(rhs, i) = weight * value;
        sum_weights += weight;
        sum_values += weight * value;
    }
    if (sum_weights <= 0) {
        return core::failure(core::ErrorCode::method_inapplicable, "No measured log-surface cells");
    }
    std::ranges::fill(logarithms, sum_values / sum_weights);
    return {};
}
void precondition(const SurfaceSystem& system, image::PlaneView<double> work) {
    const auto r = work.row(residual_row);
    auto const z = work.row(preconditioned_row);
    for (std::size_t i = 0; i < r.size(); ++i) {
        const auto columns = system.grid.columns;
        const unsigned degree = static_cast<unsigned>(i % columns != 0) +
                                static_cast<unsigned>((i % columns) + 1 < columns) +
                                static_cast<unsigned>(i >= columns) +
                                static_cast<unsigned>(i + columns < r.size());
        const double diagonal = surface_at(system.weights, i) + (system.smooth * degree);
        surface_at(z, i) = surface_at(r, i) / diagonal;
    }
}
core::Result<void> residual(const SurfaceSystem& system, image::PlaneView<double> work,
                            std::span<const double> logarithms, SolverReport& report,
                            const core::Cancellation& cancellation) {
    auto const r = work.row(residual_row);
    auto result = apply_surface_system(system, logarithms, r, cancellation);
    if (!result) {
        return result;
    }
    for (std::size_t i = 0; i < r.size(); ++i) {
        surface_at(r, i) = surface_at(work.row(rhs_row), i) - surface_at(r, i);
    }
    const double norm = std::sqrt(dot(r, r));
    if (!std::isfinite(norm)) {
        return core::failure(core::ErrorCode::numerical, "Nonfinite log-surface residual");
    }
    report.residual = norm;
    return {};
}
core::Result<void> step(const SurfaceSystem& system, image::PlaneView<double> work,
                        std::span<double> logarithms, double rz,
                        const core::Cancellation& cancellation) {
    const auto p = work.row(direction_row);
    auto const ap = work.row(product_row);
    auto result = apply_surface_system(system, p, ap, cancellation);
    if (!result) {
        return result;
    }
    const double curvature = dot(p, ap);
    if (!std::isfinite(curvature) || curvature <= 0 || !std::isfinite(rz) || rz <= 0) {
        return core::failure(core::ErrorCode::numerical, "Invalid log-surface PCG curvature");
    }
    const double alpha = rz / curvature;
    for (std::size_t i = 0; i < p.size(); ++i) {
        surface_at(logarithms, i) += alpha * surface_at(p, i);
    }
    return {};
}
} // namespace
core::Result<void> solve_surface(const SurfaceSystem& system, image::PlaneView<double> work,
                                 std::span<double> logarithms, SolverReport& report,
                                 const core::Cancellation& cancellation) {
    auto initialized = initialize(system, work, logarithms);
    if (!initialized) {
        return initialized;
    }
    report = {
        .iterations = 0,
        .residual = 0,
        .tolerance = surface_relative_tolerance *
                     std::max(std::sqrt(dot(work.row(rhs_row), work.row(rhs_row))), rhs_floor),
    };
    auto current = residual(system, work, logarithms, report, cancellation);
    if (!current || report.residual <= report.tolerance) {
        return current;
    }
    precondition(system, work);
    auto const direction = work.row(direction_row);
    std::ranges::copy(work.row(preconditioned_row), direction.begin());
    double rz = dot(work.row(residual_row), work.row(preconditioned_row));
    for (unsigned iteration = 0; iteration < surface_iteration_limit; ++iteration) {
        auto moved = step(system, work, logarithms, rz, cancellation);
        if (!moved) {
            return moved;
        }
        report.iterations = iteration + 1;
        auto checked = residual(system, work, logarithms, report, cancellation);
        if (!checked || report.residual <= report.tolerance) {
            return checked;
        }
        precondition(system, work);
        const double next_rz = dot(work.row(residual_row), work.row(preconditioned_row));
        const double ratio = next_rz / rz;
        if (!std::isfinite(ratio) || ratio < 0) {
            return core::failure(core::ErrorCode::numerical, "Invalid log-surface PCG recurrence");
        }
        for (std::size_t i = 0; i < direction.size(); ++i) {
            surface_at(direction, i) =
                surface_at(work.row(preconditioned_row), i) + (ratio * surface_at(direction, i));
        }
        rz = next_rz;
    }
    return core::failure(core::ErrorCode::numerical,
                         "Log-surface PCG did not meet its residual bound");
}
} // namespace docenhance::methods
