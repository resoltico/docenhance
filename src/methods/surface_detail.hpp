// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>

namespace docenhance::methods {
template <typename Element>
[[nodiscard]] Element& surface_at(std::span<Element> values, std::size_t index) noexcept {
    return values.subspan(index, 1).front();
}
inline constexpr unsigned surface_iteration_limit = 500;
inline constexpr double surface_relative_tolerance = 1e-8;
inline constexpr std::uint32_t surface_poll_interval = 256;
// Levels halve each grid axis down to one cell: a 1 x 65536 grid needs 17.
inline constexpr unsigned surface_level_limit = 17;
// Symmetric V-cycle: damped-Jacobi sweeps before and after an over-scaled coarse correction.
inline constexpr unsigned surface_smoothing_sweeps = 2;
inline constexpr double surface_smoothing_damping = 0.8;
inline constexpr double surface_coarse_scale = 1.6;
// The background reference is this nearest-rank quantile of the measured cell quantiles.
inline constexpr double surface_reference_rank = 0.90;
[[nodiscard]] core::Result<SurfaceGrid> surface_grid(image::Extent extent, const Surface& method);
[[nodiscard]] bool protected_at(image::PlaneView<const std::uint8_t> mask, std::uint32_t x,
                                std::uint32_t y) noexcept;
[[nodiscard]] double select_quantile(std::span<double> samples, double quantile);
struct MeasurementContext {
    std::reference_wrapper<const Surface> method;
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const core::Cancellation> cancellation;
};
[[nodiscard]] core::Result<void> measure_cells(SurfaceInput input, const SurfaceGrid& grid,
                                               image::PlaneView<double> measurements,
                                               MeasurementContext context);
struct SurfaceSystem {
    SurfaceGrid grid;
    std::span<const double> weights;
    double smooth{};
    unsigned iteration_limit = surface_iteration_limit;
};
struct SurfaceLevel {
    std::uint32_t columns{};
    std::uint32_t rows{};
    std::size_t offset{};
};
// Piecewise-constant aggregation of 2 x 2 cells with exact Galerkin coarse operators, which keep
// the weighted four-neighbor form: coarse weights and edge weights are sums of fine ones.
class SurfaceHierarchy {
  public:
    [[nodiscard]] static core::Result<SurfaceHierarchy> build(const SurfaceSystem& system,
                                                              core::Budget& budget);
    // One symmetric V-cycle, output = M^-1 residual; M is symmetric positive definite.
    [[nodiscard]] core::Result<void> precondition(std::span<const double> residual,
                                                  std::span<double> output,
                                                  const core::Cancellation& cancellation);
    [[nodiscard]] std::span<const SurfaceLevel> levels() const noexcept {
        return std::span{levels_}.first(count_);
    }

  private:
    SurfaceHierarchy(image::Plane<double> storage, double smooth) noexcept
        : storage_(std::move(storage)), smooth_(smooth) {}
    image::Plane<double> storage_;
    std::array<SurfaceLevel, surface_level_limit> levels_{};
    unsigned count_ = 0;
    double smooth_{};
};
[[nodiscard]] core::Result<void> apply_surface_system(const SurfaceSystem& system,
                                                      std::span<const double> vector,
                                                      std::span<double> output,
                                                      const core::Cancellation& cancellation);
struct SolverContext {
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const core::Cancellation> cancellation;
};
[[nodiscard]] core::Result<void> solve_surface(const SurfaceSystem& system,
                                               image::PlaneView<double> work,
                                               std::span<double> logarithms, SolverReport& report,
                                               SolverContext context);
[[nodiscard]] core::Result<SurfaceMeasurements>
measure_surface(SurfaceInput input, const SurfaceModel& model, core::Budget& budget,
                const core::Cancellation& cancellation);
// Independent predicates, exposed to unit tests so each threshold is challenged at its boundary.
[[nodiscard]] bool surface_eligible(IlluminationReport& report);
} // namespace docenhance::methods
