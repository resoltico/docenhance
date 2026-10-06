// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/reviewed_methods.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <variant>

namespace docenhance::methods {
// Mask is already in oriented coordinates; an empty view means no protected samples.
struct IlluminationInput {
    std::reference_wrapper<image::LinearSource> source;
    image::PlaneView<const std::uint8_t> protection;

    IlluminationInput(image::LinearSource& linear,
                      image::PlaneView<const std::uint8_t> mask) noexcept
        : source(linear), protection(mask) {}
};
inline constexpr double illumination_floor = 0.02;
enum class SurfaceMode { explicit_surface, automatic };
inline constexpr double illumination_default_strength = 1.0;
inline constexpr double illumination_default_max_gain = 2.0;
inline constexpr double surface_default_quantile = 0.90;
inline constexpr double surface_default_smooth = 1.0;
// The largest admissible max_gain; no illumination correction can brighten a sample further.
inline constexpr double illumination_gain_limit = 4;
struct SurfaceParameters {
    SurfaceMode mode = SurfaceMode::explicit_surface;
    double strength = illumination_default_strength;
    double max_gain = illumination_default_max_gain;
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
    [[nodiscard]] SurfaceParameters parameters() const noexcept {
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
struct MorphologyParameters {
    double strength = illumination_default_strength;
    double max_gain = illumination_default_max_gain;
    std::optional<double> target = std::nullopt;
    std::optional<std::uint32_t> radius = std::nullopt;
    bool operator==(const MorphologyParameters&) const = default;
};
class Morphology {
  public:
    static constexpr std::uint32_t min_radius = 1;
    static constexpr std::uint32_t max_radius = 256;
    [[nodiscard]] static core::Result<Morphology> create(MorphologyParameters parameters = {});
    [[nodiscard]] MorphologyParameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return morph_descriptor;
    }

  private:
    explicit Morphology(MorphologyParameters parameters) noexcept : parameters_(parameters) {}
    MorphologyParameters parameters_;
};
using Illumination = std::variant<IlluminationOff, Surface, Morphology>;
using IlluminationParameters = std::variant<SurfaceParameters, MorphologyParameters>;
struct MorphologyMeasurements {
    std::uint32_t radius{};
    double sigma{};
    std::uint32_t gaussian_radius{};
    std::uint32_t stride{};
    std::uint32_t count{};
    bool fallback = false;
    std::optional<double> analysis_fill = std::nullopt;
    std::optional<double> target = std::nullopt;
    double background_q10{};
    double background_q50{};
    double background_q90{};
    double background_min{};
    double background_max{};
    std::uint64_t field_bytes{};
    std::uint64_t preparation_charge_peak{};
};
enum class IlluminationStatus { disabled, no_change, skipped, applied, failed };
enum class IlluminationReason {
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
    IlluminationStatus status = IlluminationStatus::disabled;
    bool complete = false;
    IlluminationReason reason = IlluminationReason::none;
    std::optional<IlluminationParameters> requested = std::nullopt;
    std::optional<std::uint32_t> cell = std::nullopt;
    std::uint64_t eligible_samples{};
    std::uint64_t protected_samples{};
    std::uint32_t cells{};
    std::uint32_t measured_cells{};
    // Cells below background_reference / illumination_gain_limit, left unmeasured as dark content.
    std::uint32_t dark_cells{};
    std::optional<double> background_reference = std::nullopt;
    std::optional<SolverReport> solver = std::nullopt;
    std::optional<SurfaceMeasurements> measurements = std::nullopt;
    std::optional<MorphologyMeasurements> morphology = std::nullopt;
    std::array<std::optional<bool>, surface_predicates> predicates{};
    std::uint64_t changed_samples{};
    std::uint64_t gain_capped_samples{};
    std::uint64_t saturated_samples{};
    std::uint64_t evaluated_samples{};
    double min_gain = 1;
    double max_gain = 1;
};
struct IlluminationGain {
    double target{};
    double strength{};
    double max_gain{};
};
[[nodiscard]] core::Result<void> apply_illumination_pixel(std::span<double> rgb, double background,
                                                          IlluminationGain gain,
                                                          IlluminationReport& report);
[[nodiscard]] bool valid_morphology_report(const IlluminationReport& report, image::Extent extent,
                                           bool protection);
[[nodiscard]] bool valid_illumination(const IlluminationReport& report, image::Extent extent,
                                      bool protection);
} // namespace docenhance::methods
