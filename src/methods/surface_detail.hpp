// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>

namespace docenhance::methods {
template <typename Element>
[[nodiscard]] Element& surface_at(std::span<Element> values, std::size_t index) noexcept {
    return values.subspan(index, 1).front();
}
inline constexpr std::uint64_t surface_minimum_samples = 16;
inline constexpr double surface_explicit_coverage = 0.25;
inline constexpr double surface_automatic_coverage = 0.60;
inline constexpr double surface_relative_tolerance = 1e-8;
inline constexpr double surface_rhs_floor = 1e-12;
inline constexpr std::uint32_t surface_lattice_extent = 1024;
[[nodiscard]] SurfaceGrid surface_grid_shape(image::Extent extent, const Surface& method) noexcept;
[[nodiscard]] std::uint32_t surface_cell_requirement(std::uint32_t area) noexcept;
struct SurfaceCell {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
};
[[nodiscard]] SurfaceCell surface_cell(const SurfaceGrid& grid, std::uint32_t index) noexcept;
[[nodiscard]] core::Result<void> resolve_surface_cell(std::span<double> samples, std::uint32_t area,
                                                      const Surface& method,
                                                      std::span<double> output,
                                                      const core::Cancellation& cancellation);
[[nodiscard]] core::Result<void> exclude_surface_dark_cells(image::PlaneView<double> work,
                                                            std::optional<double>& reference,
                                                            std::uint32_t& dark_cells,
                                                            const core::Cancellation& cancellation);
// Valid grid/coordinate and producer-admitted finite coefficients. This is a raw IEEE result;
// actual sample/map producers admit finite positive (and, for gain, Unit) B exactly once.
[[nodiscard]] double surface_background_value(const SurfaceGrid& grid,
                                              image::PlaneView<const double> logarithms,
                                              image::Coordinate coordinate) noexcept;
struct SurfaceSampleCounts {
    std::uint32_t stride{};
    std::uint32_t count{};
    bool fallback = false;
    std::uint32_t paper{};
    std::uint32_t dark{};
};
[[nodiscard]] core::Result<SurfaceMeasurements>
surface_sample_measurements(std::span<double> y, std::span<double> b, SurfaceSampleCounts measured,
                            const core::Cancellation& cancellation);
[[nodiscard]] std::uint64_t surface_minimum_support(const SurfaceGrid& grid,
                                                    std::uint32_t positive_cells) noexcept;
[[nodiscard]] std::optional<std::uint32_t> surface_fraction_count(double fraction,
                                                                  std::uint32_t samples) noexcept;
[[nodiscard]] inline bool surface_paper_sample(double source, double background) noexcept {
    constexpr double ratio = 0.8;
    return source >= ratio * background;
}
[[nodiscard]] inline bool surface_dark_sample(double source, double background) noexcept {
    constexpr double ratio = 0.75;
    return source < ratio * background;
}
[[nodiscard]] double surface_coverage(std::uint32_t measured, std::uint32_t cells) noexcept;
[[nodiscard]] double surface_variation(double low, double high) noexcept;
[[nodiscard]] std::uint32_t surface_lattice_stride(image::Extent extent) noexcept;
[[nodiscard]] SurfacePredicates surface_decisions(const SurfaceMeasurements& measurements,
                                                  double coverage) noexcept;
inline constexpr std::uint32_t surface_poll_interval = 256;
// Levels halve each grid axis down to one cell: a 1 x 65536 grid needs 17.
inline constexpr unsigned surface_level_limit = 17;
// Symmetric V-cycle: damped-Jacobi sweeps before and after an over-scaled coarse correction.
inline constexpr unsigned surface_smoothing_sweeps = 2;
inline constexpr double surface_smoothing_damping = 0.8;
inline constexpr double surface_coarse_scale = 1.6;
// The background reference is this nearest-rank quantile of the measured cell quantiles.
inline constexpr double surface_reference_rank = 0.90;
[[nodiscard]] bool protected_at(image::PlaneView<const std::uint8_t> mask, std::uint32_t x,
                                std::uint32_t y) noexcept;
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
} // namespace docenhance::methods
