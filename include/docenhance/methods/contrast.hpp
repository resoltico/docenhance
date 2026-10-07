// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/reviewed_methods.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
namespace docenhance::methods {
inline constexpr double contrast_default_blend = 1;
inline constexpr double levels_default_low = 0.5;
inline constexpr double levels_default_high = 99.5;
inline constexpr double gamma_default_exponent = 1.2;
inline constexpr double levels_minimum_range = 1e-6;
struct LevelsParameters {
    double low = levels_default_low;
    double high = levels_default_high;
    double blend = contrast_default_blend;
    bool operator==(const LevelsParameters&) const = default;
};
struct GammaParameters {
    double gamma = gamma_default_exponent;
    double blend = contrast_default_blend;
    bool operator==(const GammaParameters&) const = default;
};
class Levels {
  public:
    static constexpr double maximum_low = 10;
    static constexpr double minimum_high = 90;
    static constexpr double maximum_high = 100;
    [[nodiscard]] static core::Result<Levels> create(LevelsParameters p = {});
    [[nodiscard]] LevelsParameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return levels_descriptor;
    }

  private:
    explicit Levels(LevelsParameters p) : parameters_(p) {}
    LevelsParameters parameters_;
};
class Gamma {
  public:
    static constexpr double minimum_exponent = 0.25;
    static constexpr double maximum_exponent = 4;
    [[nodiscard]] static core::Result<Gamma> create(GammaParameters p = {});
    [[nodiscard]] GammaParameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return gamma_descriptor;
    }

  private:
    explicit Gamma(GammaParameters p) : parameters_(p) {}
    GammaParameters parameters_;
};
inline constexpr std::uint32_t clahe_default_grid = 8;
inline constexpr std::uint32_t clahe_maximum_grid = 32;
inline constexpr double clahe_maximum_clip = 8;
inline constexpr std::uint32_t clahe_minimum_tile = 16;
inline constexpr std::uint32_t clahe_minimum_samples = 16;
struct ClaheParameters {
    std::uint32_t grid_columns = clahe_default_grid;
    std::uint32_t grid_rows = clahe_default_grid;
    double clip = 2;
    double blend = contrast_default_blend;
    bool operator==(const ClaheParameters&) const = default;
};
class Clahe {
  public:
    [[nodiscard]] static core::Result<Clahe> create(ClaheParameters p = {});
    [[nodiscard]] ClaheParameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return clahe_descriptor;
    }

  private:
    explicit Clahe(ClaheParameters p) : parameters_(p) {}
    ClaheParameters parameters_;
};
inline constexpr std::uint32_t clahe_bins = 1024;
struct ClaheMaps {
    image::Plane<double> knots;
    std::array<bool, std::size_t{clahe_maximum_grid} * clahe_maximum_grid> identity{};
};
struct ContrastOff {};
using Contrast = std::variant<ContrastOff, Levels, Gamma, Clahe>;
using ContrastParameters = std::variant<LevelsParameters, GammaParameters, ClaheParameters>;
enum class ContrastStatus { disabled, no_change, applied, failed };
enum class ContrastReason {
    none,
    zero_blend,
    no_eligible_samples,
    identity_gamma,
    insufficient_dynamic_range,
    no_effect,
    processing_failure,
};
struct LevelsRange {
    double low = 0;
    double high = 0;
    bool operator==(const LevelsRange&) const = default;
};
struct ContrastReport {
    ContrastStatus status = ContrastStatus::disabled;
    ContrastReason reason = ContrastReason::none;
    bool complete = false;
    std::optional<ContrastParameters> requested = std::nullopt;
    std::optional<LevelsRange> levels = std::nullopt;
    std::uint64_t eligible_samples = 0;
    std::uint64_t protected_samples = 0;
    std::uint64_t measured_samples = 0;
    std::uint32_t identity_tiles = 0;
    std::uint64_t evaluated_samples = 0;
    std::uint64_t corrected_samples = 0;
    std::uint64_t changed_samples = 0;
    std::uint64_t clipped_low_samples = 0;
    std::uint64_t clipped_high_samples = 0;
    std::uint64_t preparation_charge_peak = 0;
    bool operator==(const ContrastReport&) const = default;
};
[[nodiscard]] std::string_view status_name(ContrastStatus value) noexcept;
[[nodiscard]] std::string_view reason_name(ContrastReason value) noexcept;
[[nodiscard]] core::Result<double> levels_candidate(double f, LevelsRange range);
[[nodiscard]] core::Result<double> gamma_candidate(double f, const Gamma& method);
[[nodiscard]] bool valid_contrast_observations(const ContrastReport& report);
[[nodiscard]] bool valid_contrast(const ContrastReport& report, const Contrast& method);
struct ContrastExecution {
    ContrastExecution(core::Budget& b, core::Cancellation c, ContrastReport& r, image::RowUse use)
        : budget(b), cancellation(std::move(c)), report(r), preparation_use(use) {}
    std::reference_wrapper<core::Budget> budget;
    core::Cancellation cancellation;
    std::reference_wrapper<ContrastReport> report;
    image::RowUse preparation_use;
};
class ContrastModel {
  public:
    [[nodiscard]] static core::Result<ContrastModel>
    prepare(image::LinearSource& source, image::PlaneView<const std::uint8_t> mask,
            const Contrast& method, const ContrastExecution& execution);
    [[nodiscard]] bool active() const noexcept {
        return active_;
    }
    [[nodiscard]] bool measured_source() const noexcept {
        return measured_;
    }
    [[nodiscard]] core::Result<void> apply(image::RowRange range, std::span<double> rgb,
                                           image::PlaneView<const std::uint8_t> mask,
                                           ContrastReport& report,
                                           const core::Cancellation& cancellation) const;

  private:
    ContrastModel(image::Extent extent, Contrast method, std::optional<LevelsRange> levels,
                  bool active, bool measured)
        : extent_(extent), method_(method), levels_(levels), active_(active), measured_(measured) {}
    [[nodiscard]] core::Result<image::Rgb>
    map_pixel(const image::Rgb& before, image::RowRange position, ContrastReport& report) const;
    image::Extent extent_;
    Contrast method_;
    std::optional<LevelsRange> levels_;
    bool active_;
    bool measured_;
    ClaheMaps maps_;
};
} // namespace docenhance::methods
