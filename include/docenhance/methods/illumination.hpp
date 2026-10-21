// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/method_catalog.hpp"
#include "docenhance/methods/parameters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>
#include <variant>

namespace docenhance::methods {
inline constexpr double illumination_floor = 0.02;
// The largest admissible max_gain; no illumination correction can brighten a sample further.
class Surface {
  public:
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
class Morphology {
  public:
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
inline constexpr unsigned surface_iteration_limit = 500;
inline constexpr std::uint32_t surface_sample_limit = 1048576;
struct SolverReport {
    unsigned iterations{};
    double residual{};
    double tolerance{};
    bool operator==(const SolverReport&) const = default;
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
    bool operator==(const SurfaceMeasurements&) const = default;
};
// The six predicates are in documented order: coverage, bright quantile, median background,
// background variation, paper fraction, dark fraction. Null means not evaluated, never a pass.
inline constexpr std::size_t surface_predicates = 6;
struct SurfacePredicates {
    std::optional<bool> coverage = std::nullopt;
    std::optional<bool> bright_quantile = std::nullopt;
    std::optional<bool> median_background = std::nullopt;
    std::optional<bool> background_variation = std::nullopt;
    std::optional<bool> paper_fraction = std::nullopt;
    std::optional<bool> dark_fraction = std::nullopt;
    [[nodiscard]] std::array<std::optional<bool>, surface_predicates> values() const noexcept {
        return {
            coverage,       bright_quantile, median_background, background_variation,
            paper_fraction, dark_fraction,
        };
    }
    bool operator==(const SurfacePredicates&) const = default;
};
[[nodiscard]] bool validate(const SolverReport& solution) noexcept;
[[nodiscard]] bool validate(const SurfaceMeasurements& measurements) noexcept;
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::Fields<methods::SolverReport> {
    using Value = methods::SolverReport;
    static constexpr auto nonnegative = core::Range<double>{
        .minimum = 0,
        .maximum = std::numeric_limits<double>::max(),
    };
    static constexpr auto positive = core::Range<double>{
        .minimum = std::numeric_limits<double>::denorm_min(),
        .maximum = std::numeric_limits<double>::max(),
    };
    static constexpr auto list = std::tuple{
        core::field(
            "iterations", &Value::iterations,
            core::Range<unsigned>{.minimum = 0, .maximum = methods::surface_iteration_limit}),
        core::field("residual", &Value::residual, nonnegative),
        core::field("tolerance", &Value::tolerance, positive),
    };
};
template <> struct core::Fields<methods::SurfaceMeasurements> {
    using Value = methods::SurfaceMeasurements;
    static constexpr auto positive_unit = core::Range<double>{
        .minimum = std::numeric_limits<double>::denorm_min(),
        .maximum = 1,
    };
    static constexpr auto list = std::tuple{
        core::field("stride", &Value::stride, core::Count{}),
        core::field(
            "count", &Value::count,
            core::Range<std::uint32_t>{.minimum = 1, .maximum = methods::surface_sample_limit}),
        core::field("fallback", &Value::fallback),
        core::field("target", &Value::target, positive_unit),
        core::field("background_q10", &Value::background_q10, positive_unit),
        core::field("background_q50", &Value::background_q50, positive_unit),
        core::field("background_q90", &Value::background_q90, positive_unit),
        core::field("luminance_q90", &Value::luminance_q90, core::Unit{}),
        core::field("variation", &Value::variation, core::Unit{}),
        core::field("paper_fraction", &Value::paper_fraction, core::Unit{}),
        core::field("dark_fraction", &Value::dark_fraction, core::Unit{}),
    };
};
template <> struct core::Fields<methods::SurfacePredicates> {
    using Value = methods::SurfacePredicates;
    static constexpr auto list = std::tuple{
        core::field("coverage", &Value::coverage),
        core::field("bright_quantile", &Value::bright_quantile),
        core::field("median_background", &Value::median_background),
        core::field("background_variation", &Value::background_variation),
        core::field("paper_fraction", &Value::paper_fraction),
        core::field("dark_fraction", &Value::dark_fraction),
    };
};
} // namespace docenhance
