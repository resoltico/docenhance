// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/method_catalog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace docenhance::methods {
enum class SurfaceMode { explicit_surface, automatic };
inline constexpr double surface_default_strength = 1.0;
inline constexpr double surface_default_max_gain = 2.0;
inline constexpr double surface_default_quantile = 0.90;
inline constexpr double surface_default_smooth = 1.0;
// The largest admissible max_gain; no I01 correction can brighten a sample further.
inline constexpr double surface_gain_limit = 4;
struct SurfaceParameters {
    SurfaceMode mode = SurfaceMode::explicit_surface;
    double strength = surface_default_strength;
    double max_gain = surface_default_max_gain;
    std::optional<double> target = std::nullopt;
    std::optional<std::uint32_t> cell = std::nullopt;
    double quantile = surface_default_quantile;
    double smooth = surface_default_smooth;
    bool operator==(const SurfaceParameters&) const = default;
};
class Surface {
  public:
    static constexpr std::uint32_t min_cell = 8;
    static constexpr std::uint32_t max_cell = 512;
    [[nodiscard]] static core::Result<Surface> create(SurfaceParameters parameters = {});
    [[nodiscard]] const SurfaceParameters& parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return surface_descriptor;
    }

  private:
    explicit Surface(SurfaceParameters parameters) noexcept : parameters_(parameters) {}
    SurfaceParameters parameters_;
};
struct IlluminationOff {};
using Illumination = std::variant<IlluminationOff, Surface>;
enum class SurfaceStatus { disabled, no_change, skipped, applied, failed };
enum class SurfaceReason {
    none,
    zero_strength,
    unit_gain,
    no_eligible_samples,
    insufficient_samples,
    insufficient_cells,
    automatic_predicates,
    no_effect,
    processing_failure,
};
struct SolverReport {
    unsigned iterations{};
    double residual{};
    double tolerance{};
};
struct SurfaceMeasurements {
    std::uint32_t stride{};
    std::uint32_t count{};
    bool fallback = false;
    double target{};
    double background_q10{};
    double background_q50{};
    double background_q90{};
    double luminance_q90{};
    double variation{};
    double paper_fraction{};
    double dark_fraction{};
};
// The six predicates are in documented order: coverage, bright quantile, median background,
// background variation, paper fraction, dark fraction. Null means not evaluated, never a pass.
inline constexpr std::size_t surface_predicates = 6;
struct IlluminationReport {
    SurfaceStatus status = SurfaceStatus::disabled;
    bool complete = false;
    SurfaceReason reason = SurfaceReason::none;
    std::optional<SurfaceParameters> requested = std::nullopt;
    std::optional<std::uint32_t> cell = std::nullopt;
    std::uint64_t eligible_samples{};
    std::uint64_t protected_samples{};
    std::uint32_t cells{};
    std::uint32_t measured_cells{};
    // Cells below background_reference / surface_gain_limit, left unmeasured as dark content.
    std::uint32_t dark_cells{};
    std::optional<double> background_reference = std::nullopt;
    std::optional<SolverReport> solver = std::nullopt;
    std::optional<SurfaceMeasurements> measurements = std::nullopt;
    std::array<std::optional<bool>, surface_predicates> predicates{};
    std::uint64_t changed_samples{};
    std::uint64_t gain_capped_samples{};
    std::uint64_t saturated_samples{};
    std::uint64_t evaluated_samples{};
    double min_gain = 1;
    double max_gain = 1;
};
} // namespace docenhance::methods
